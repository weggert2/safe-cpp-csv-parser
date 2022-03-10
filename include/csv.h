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

} // end namespace error

} // end namespace io

#endif // CSV_H