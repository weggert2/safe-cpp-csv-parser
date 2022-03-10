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
#include <string>
#include <utility>
#include <vector>

namespace io
{

////////////////////////////////////////////////////////////////////////////
//                          LineReader Errors                             //
////////////////////////////////////////////////////////////////////////////
namespace error
{

static constexpr int max_file_name_length = 255;

/**
 * Base class for errors.
 */
class base
{
public:
    /**
     * Format the error message.
     */
    virtual void format_error_message() const = 0;

    /**
     * Format and return the error message.
     */
    const char *what() const noexcept
    {
        format_error_message();
        return error_message_buffer;
    }

public:
    /**
     * The error message buffer.
     */
    mutable char error_message_buffer[512];
};

/**
 * For errors with file names.
 */
class with_file_name
{
public:
    with_file_name()
    {
        std::memset(file_name, 0, sizeof(file_name));
    }

    void set_file_name(const char *file_name_)
    {
        if(file_name != nullptr)
        {
            /*
             * This call to strncpy has parenthesis around it
             * to silence the GCC -Wstringop-truncation warning.
             */
            (strncpy(file_name, file_name_, sizeof(file_name)));
            file_name[sizeof(file_name)-1] = '\0';
        }
        else
        {
            file_name[0] = '\0';
        }
    }

    const char *get_file_name() const
    {
        return file_name;
    }

private:
    /**
     * Buffer.
     */
    char file_name[max_file_name_length+1];
};

/**
 * For errors with file lines.
 */
class with_file_line
{
public:
    with_file_line():
        file_line(-1)
    {}

    void set_file_line(int file_line_)
    {
        file_line = file_line_;
    }

    int get_file_line() const
    {
        return file_line;
    }
private:
    int file_line;
};

/**
 * For errors from errno.
 */
class with_errno
{
public:
    with_errno():
        errno_(0)
    {}

    void set_errno(int errno__)
    {
        errno_ = errno__;
    }

    int get_errno() const
    {
        return errno_;
    }
private:
    int errno_;
};

class cannot_open_file :
    public base,
    public with_file_name,
    public with_errno
{
public:
    void format_error_message() const override
    {
        if (get_errno() != 0)
        {
            std::snprintf(
                error_message_buffer, sizeof(error_message_buffer),
                "Can not open file \"%s\" because \"%s\".",
                get_file_name(), std::strerror(get_errno()));
        }
        else
        {
            std::snprintf(
                error_message_buffer, sizeof(error_message_buffer),
                "Can not open file \"%s\".",
                get_file_name());
        }
    }
};

class line_length_limit_exceeded :
    public base,
    public with_file_name,
    public with_file_line
{
public:
    void format_error_message()const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            "Line number %d in file \"%s\" exceeds the maximum length of 2^24-1.",
            get_file_line(), get_file_name());
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
//                                 LineReader                             //
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
    std::string err;
    bool valid = false;

private:
    static std::unique_ptr<ByteSourceBase> open_file(
        const char*file_name,
        std::string &err_)
    {
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
            error::cannot_open_file err__;
            err__.set_errno(x);
            err__.set_file_name(file_name);
            err__.format_error_message();
            err_ = std::string(err__.what());

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

    explicit LineReader(const char *file_name_)
    {
        set_file_name(file_name_);
        valid = init(open_file(file_name, err));
    }

    explicit LineReader(const std::string &file_name)
    {
        set_file_name(file_name.c_str());
        valid = init(open_file(file_name.c_str(), err));
    }

    LineReader(
        const char *file_name,
        std::unique_ptr<ByteSourceBase> byte_source)
    {
        set_file_name(file_name);
        valid = init(std::move(byte_source));
    }

    LineReader(
        const std::string &file_name,
        std::unique_ptr<ByteSourceBase>byte_source)
    {
        set_file_name(file_name.c_str());
        valid = init(std::move(byte_source));
    }

    LineReader(
        const char *file_name,
        const char *data_begin,
        const char *data_end)
    {
        set_file_name(file_name);
        valid = init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningStringByteSource(data_begin, data_end-data_begin)));
    }

    LineReader(
        const std::string &file_name,
        const char *data_begin,
        const char *data_end)
    {
        set_file_name(file_name.c_str());
        valid = init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningStringByteSource(data_begin, data_end-data_begin)));
    }

    LineReader(
        const char *file_name,
        FILE *file)
    {
        set_file_name(file_name);
        valid = init(std::unique_ptr<ByteSourceBase>(
            new detail::OwningStdIOByteSourceBase(file)));
    }

    LineReader(
        const std::string &file_name,
        FILE *file)
    {
        set_file_name(file_name.c_str());
        valid = init(std::unique_ptr<ByteSourceBase>(
            new detail::OwningStdIOByteSourceBase(file)));
    }

    LineReader(
        const char *file_name,
        std::istream &in)
    {
        set_file_name(file_name);
        valid = init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningIStreamByteSource(in)));
    }

    LineReader(
        const std::string &file_name,
        std::istream &in)
    {
        set_file_name(file_name.c_str());
        valid = init(std::unique_ptr<ByteSourceBase>(
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

    char *next_line()
    {
        if(data_begin == data_end)
        {
            return nullptr;
        }

        ++file_line;

        if (data_begin >= data_end)
        {
            err += "Internal error: data_begin >= data_end\n";
            return nullptr;
        }
        if (data_end > block_len*2)
        {
            err += "Internal error: data_end > block_len*2\n";
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
            error::line_length_limit_exceeded err_;
            err_.set_file_name(file_name);
            err_.set_file_line(file_line);
            err_.format_error_message();
            err += std::string(err_.what());
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

    const std::string &get_err() const { return err; }
};

////////////////////////////////////////////////////////////////////////////
//                           CsvReader Errors                             //
////////////////////////////////////////////////////////////////////////////

namespace error
{

constexpr int max_column_name_length = 63;
constexpr int max_column_content_length = 63;

class with_column_name
{
public:
    with_column_name()
    {
        std::memset(column_name, 0, max_column_name_length+1);
    }

    void set_column_name(const char *column_name_)
    {
        if(column_name_ != nullptr)
        {
            std::strncpy(column_name, column_name_, max_column_name_length);
            column_name[max_column_name_length] = '\0';
        }
        else
        {
            column_name[0] = '\0';
        }
    }

    const char *get_column_name() const { return column_name; }
private:
    char column_name[max_column_name_length+1];
};

class with_column_content
{
public:
    with_column_content()
    {
        std::memset(column_content, 0, max_column_content_length+1);
    }

    void set_column_content(const char *column_content_)
    {
        if(column_content_ != nullptr)
        {
            std::strncpy(column_content, column_content_, max_column_content_length);
            column_content[max_column_content_length] = '\0';
        }
        else
        {
            column_content[0] = '\0';
        }
    }

    const char *get_column_content() const { return column_content; }

private:
    char column_content[max_column_content_length+1];
};

class extra_column_in_header :
    public base,
    public with_file_name,
    public with_column_name
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(Extra column "%s" in header of file "%s".)",
            get_column_name(), get_file_name());
    }
};

class missing_column_in_header :
    public base,
    public with_file_name,
    public with_column_name
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(Missing column "%s" in header of file "%s".)",
            get_column_name(), get_file_name());
    }
};

class duplicated_column_in_header :
    public base,
    public with_file_name,
    public with_column_name
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(Duplicated column "%s" in header of file "%s".)",
            get_column_name(), get_file_name());
    }
};

class header_missing :
    public base,
    public with_file_name
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            "Header missing in file \"%s\".",
            get_file_name());
    }
};

class too_few_columns :
    public base,
    public with_file_name,
    public with_file_line
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            "Too few columns in line %d in file \"%s\".",
            get_file_line(), get_file_name());
    }
};

class too_many_columns :
    public base,
    public with_file_name,
    public with_file_line
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            "Too many columns in line %d in file \"%s\".",
            get_file_line(), get_file_name());
    }
};

class escaped_string_not_closed :
    public base,
    public with_file_name,
    public with_file_line
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            "Escaped string was not closed in line %d in file \"%s\".",
            get_file_line(), get_file_name());
    }
};

class integer_must_be_positive :
    public base,
    public with_file_name,
    public with_file_line,
    public with_column_name,
    public with_column_content
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(The integer "%s" must be positive or 0 in column "%s" in file "%s" in line "%d".)",
            get_column_content(), get_column_name(), get_file_name(), get_file_line());
    }
};

class no_digit :
    public base,
    public with_file_name,
    public with_file_line,
    public with_column_name,
    public with_column_content
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(The integer "%s" contains an invalid digit in column "%s" in file "%s" in line "%d".)",
            get_column_content(), get_column_name(), get_file_name(), get_file_line());
    }
};

class integer_overflow :
    public base,
    public with_file_name,
    public with_file_line,
    public with_column_name,
    public with_column_content
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(The integer "%s" overflows in column "%s" in file "%s" in line "%d".)",
            get_column_content(), get_column_name(), get_file_name(), get_file_line());
    }
};

class integer_underflow :
    public base,
    public with_file_name,
    public with_file_line,
    public with_column_name,
    public with_column_content
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(The integer "%s" underflows in column "%s" in file "%s" in line "%d".)",
            get_column_content(), get_column_name(), get_file_name(), get_file_line());
    }
};

class invalid_single_character :
    public base,
    public with_file_name,
    public with_file_line,
    public with_column_name,
    public with_column_content
{
public:
    void format_error_message() const override
    {
        std::snprintf(
            error_message_buffer, sizeof(error_message_buffer),
            R"(The content "%s" of column "%s" in file "%s" in line "%d" is not a single character.)",
            get_column_content(), get_column_name(), get_file_name(), get_file_line());
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
    constexpr static bool is_trim_char(char c)
    {
        (void)c;
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
        std::string &err)
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
        std::string &err)
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
                            error::escaped_string_not_closed err_;
                            err_.format_error_message();
                            err = std::string(err_.what());
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
    std::string &err)
{
    if (line == nullptr)
    {
        err += "Internal error: line is null in chop_next_column\n";
        return false;
    }

    col_begin = line;

    /* The col_begin + (... - col_begin) removes the constness */
    col_end = col_begin + (quote_policy::find_next_column_end(col_begin, err) - col_begin);

    if (!err.empty())
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
   std::string &err)
{
    for (int i : col_order)
    {
        if(line == nullptr)
        {
            ::io::error::too_few_columns err_;
            err_.format_error_message();
            err = std::string(err_.what());
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
        ::io::error::too_many_columns err_;
        err_.format_error_message();
        err = std::string(err_.what());
        return false;
    }
}

template<unsigned column_count, class trim_policy, class quote_policy>
bool parse_header_line(
    char *line,
    std::vector<int> &col_order,
    const std::string *col_name,
    ignore_column ignore_policy,
    std::string &err)
{
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
                    error::duplicated_column_in_header err_;
                    err_.set_column_name(col_begin);
                    err_.format_error_message();
                    err = std::string(err_.what());
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
                error::extra_column_in_header err_;
                err_.set_column_name(col_begin);
                err_.format_error_message();
                err = std::string(err_.what());
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
                error::missing_column_in_header err_;
                err_.set_column_name(col_name[i].c_str());
                err_.format_error_message();
                err = std::string(err_.what());
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
    std::string &err)
{
    if(!*col)
    {
        error::invalid_single_character err_;
        err_.format_error_message();
        err = std::string(err_.what());
        return false;
    }

    x = *col;
    ++col;

    if(*col)
    {
        error::invalid_single_character err_;
        err_.format_error_message();
        err = std::string(err_.what());
        return false;
    }

    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    std::string &x,
    std::string &err)
{
    (void)err;
    x = col;
    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    const char *&x,
    std::string &err)
{
    (void)err;
    x = col;
    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    char *&x,
    std::string &err)
{
    (void)err;
    x = col;
    return true;
}

template<class overflow_policy, class T>
bool parse_unsigned_integer(
    const char *col,
    T &x,
    std::string &err)
{
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
            error::no_digit err_;
            err_.format_error_message();
            err = std::string(err_.what());
            return false;
        }
        ++col;
    }

    return true;
}

} // end namespace detail

} // end namespace io

#endif // CSV_H