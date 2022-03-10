/**
 * Copyright: (2022) Bill Eggert b eggert at proton mail dot com
 * License: BSD-3
 *
 * This code is taken directly from Ben Strasser's excellent 'fast-cpp-csv-parser'
 * https://github.com/ben-strasser/fast-cpp-csv-parser, and modified such that
 * it does not throw. This makes it usable in situations where -fno-exceptions
 * is required.
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

/**
 * Utilities for handling errors.
 */
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

private:
    /**
     * The error message buffer.
     */
    mutable char error_message_buffer[512];
};

/**
 * Struct to contain errors with file names.
 */
class with_file_name
{
public:
    with_file_name()
    {
        std::memset(file_name, 0, sizeof(file_name));
    }

    /**
     * Set the file name.
     */
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
private:
    /**
     * Buffer.
     */
    char file_name[max_file_name_length+1];
};

} // end namespace error

} // end namespace io