#include "csv.h"

#include <gtest/gtest.h>

TEST(csv, nominal)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<4> reader(err, "1.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_TRUE(reader.read_header(err, io::ignore_no_column, "a","b","c","d"));
    ASSERT_FALSE(err) << err->get_error();

    int a,b,c,d;
    ASSERT_TRUE(reader.read_row(err, a,b,c,d));
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_EQ(a,1);
    ASSERT_EQ(b,2);
    ASSERT_EQ(c,3);
    ASSERT_EQ(d,4);
}

TEST(csv, cannot_open_file)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<4> reader(err, "-1.csv");
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "Can not open file \"-1.csv\" because \"No such file or directory\".");
}

TEST(csv, extra_column_in_header)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<1> reader(err, "2.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_FALSE(reader.read_header(err, io::ignore_no_column, "a"));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "Extra column \"b\" in header of file \"2.csv\"");
}

TEST(csv, missing_column_in_header)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "3.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_FALSE(reader.read_header(err, io::ignore_no_column, "a", "b"));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "Missing column \"b\" in header of file \"3.csv\"");
}

TEST(csv, duplicated_column_in_header)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "4.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_FALSE(reader.read_header(err, io::ignore_no_column, "a", "a"));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "Duplicated column \"a\" in header of file \"4.csv\"");
}

TEST(csv, missing_header)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "5.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_FALSE(reader.read_header(err, io::ignore_no_column, "a", "b"));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "Header missing in file \"5.csv\"");
}

TEST(csv, too_few_columns)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "6.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_TRUE(reader.read_header(err, io::ignore_no_column, "a", "b"));
    ASSERT_FALSE(err) << err->get_error();

    int a,b;
    ASSERT_TRUE(reader.read_row(err, a,b));
    ASSERT_TRUE(reader.read_row(err, a,b));
    ASSERT_FALSE(reader.read_row(err, a,b));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
         "Too few columns in line 4 in file \"6.csv\"");
}

TEST(csv, too_many_columns)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "7.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_TRUE(reader.read_header(err, io::ignore_no_column, "a", "b"));
    ASSERT_FALSE(err) << err->get_error();

    int a,b;
    ASSERT_FALSE(reader.read_row(err, a,b));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "Too many columns in line 2 in file \"7.csv\"");
}

// TODO
TEST(csv, escaped_string_not_closed)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "8.csv");
    ASSERT_FALSE(err) << err->get_error();
}

TEST(csv, integer_must_be_positive)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "9.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_TRUE(reader.read_header(err, io::ignore_no_column, "a", "b"));
    ASSERT_FALSE(err) << err->get_error();

    size_t a,b;
    ASSERT_FALSE(reader.read_row(err, a,b));
    ASSERT_TRUE(err);

    err->format_error_message();
    ASSERT_EQ(
        err->get_error(),
        "The integer -1 must be positive or 0 in column b in file 9.csv in line 2");
}

TEST(csv, no_digit)
{
    std::shared_ptr<io::error::error> err;
    io::CSVReader<2> reader(err, "10.csv");
    ASSERT_FALSE(err) << err->get_error();

    ASSERT_TRUE(reader.read_header(err, io::ignore_no_column, "a", "b"));
    ASSERT_FALSE(err) << err->get_error();

    int a,b;
    ASSERT_FALSE(reader.read_row(err, a,b));
    ASSERT_TRUE(err);

    ASSERT_EQ(
        err->get_error(),
        "The integer x contains an invalid digit in column b in file 10.csv in line 2");
}

TEST(csv, integer_overflow)
{
    // TODO
}

TEST(csv, integer_underflow)
{
    // TODO
}