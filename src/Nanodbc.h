/// \file Nanodbc.h The entirety of Nanodbc can be found within this file and Nanodbc.cpp.

/// \mainpage
///
/// \section synopsis Synopsis
/// This library provides a wrapper API for the native ODBC API. It aims to do everything ODBC does,
/// but with a \b much nicer interface. Anything it doesn't (yet) do can be done by retrieving the
/// native ODBC handles and dropping down to straight ODBC C API code.
/// For more propaganda, please see the <a href="http://lexicalunit.github.com/Nanodbc/">project
/// homepage</a>.
///
/// \section toc Table of Contents
/// - \ref license "License"
/// - \ref credits "Credits"
/// - Source level documentation:
///     - \ref Nanodbc "Nanodbc namespace"
///     - \ref exceptions
///     - \ref utility
///     - \ref mainc
///     - \ref mainf
///     - \ref binding
///     - \ref bind_multi
///     - \ref bindStrings
///
/// \section license License
/// <div class="license">
/// Copyright (C) 2013 lexicalunit <lexicalunit@lexicalunit.com>
///
/// The MIT License
///
/// Permission is hereby granted, free of charge, to any person obtaining a copy
/// of this software and associated documentation files (the "Software"), to deal
/// in the Software without restriction, including without limitation the rights
/// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
/// copies of the Software, and to permit persons to whom the Software is
/// furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included in
/// all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
/// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
/// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
/// THE SOFTWARE.
/// </div>
///
/// \section credits Credits
/// <div class="license">
/// Much of the code in this file was originally derived from TinyODBC.
/// TinyODBC is hosted at http://code.google.com/p/tiodbc/
/// Copyright (C) 2008 SqUe squarious@gmail.com
/// License: The MIT License
///
/// The idea for using RAII for transactions was inspired by SimpleDB: C++ ODBC database API,
/// however the code in Nanodbc is original and not derived from SimpleDB. Therefore
/// the LGPL license under which SimpleDB is distributed does NOT apply to Nanodbc.
/// SimpleDB is hosted at http://simpledb.sourceforge.net
/// Copyright (C) 2006 Eminence Technology Pty Ltd
/// Copyright (C) 2008-2010,2012 Russell Kliese russell@kliese.id.au
/// License: GNU Lesser General Public version 2.1
///
/// Some improvements and features are based on The Python ODBC Library.
/// The Python ODBC Library is hosted at http://code.google.com/p/pyodbc/
/// License: The MIT License
///
/// Implementation of column binding inspired by Nick E. Geht's source code posted to on CodeGuru.
/// GSODBC hosted at http://www.codeguru.com/mfc_database/gsodbc.html
/// Copyright (C) 2002 Nick E. Geht
/// License: Perpetual license to reproduce, distribute, adapt, perform, display, and sublicense.
/// See http://www.codeguru.com/submission-guidelines.php for details.
/// </div>

#ifndef NANODBC_H
#define NANODBC_H

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef __clang__
#include <cstdint>
#endif

/// \brief The entirety of Nanodbc can be found within this one namespace.
///
/// \note This library does not make any exception safety guarantees, but should work just fine with
///       a threading enabled ODBC Driver. If you want to use Nanodbc objects in threads I recommend
///       each thread keep their own Connection to the database. Otherwise you must synchronize any
///       access to Nanodbc objects.
namespace Nanodbc
{

// clang-format off
//  .d8888b.                     .d888 d8b                                   888    d8b
// d88P  Y88b                   d88P"  Y8P                                   888    Y8P
// 888    888                   888                                          888
// 888         .d88b.  88888b.  888888 888  .d88b.  888  888 888d888 8888b.  888888 888  .d88b.  88888b.
// 888        d88""88b 888 "88b 888    888 d88P"88b 888  888 888P"      "88b 888    888 d88""88b 888 "88b
// 888    888 888  888 888  888 888    888 888  888 888  888 888    .d888888 888    888 888  888 888  888
// Y88b  d88P Y88..88P 888  888 888    888 Y88b 888 Y88b 888 888    888  888 Y88b.  888 Y88..88P 888  888
//  "Y8888P"   "Y88P"  888  888 888    888  "Y88888  "Y88888 888    "Y888888  "Y888 888  "Y88P"  888  888
//                                              888
//                                         Y8b d88P
//                                          "Y88P"
// MARK: Configuration -
// clang-format on

/// \addtogroup macros Macros
/// \brief Macros that Nanodbc uses, can be overriden by users.
///
/// @{

#ifdef DOXYGEN
/// \def NANODBC_ASSERT(expression)
/// \brief Assertion.
///
/// By default, Nanodbc uses C \c assert() for internal assertions.
/// User can override it by defining \c NANODBC_ASSERT(expr) macro
/// in the Nanodbc.h file and customizing it as desired,
/// before building the library.
///
/// \code{.cpp}
/// #ifdef _DEBUG
///     #include <crtdbg.h>
///     #define NANODBC_ASSERT _ASSERTE
/// #endif
/// \endcode
#define NANODBC_ASSERT(expression) assert(expression)
#endif

/// @}

// You must explicitly request Unicode support by defining NANODBC_USE_UNICODE at compile time.
#ifndef DOXYGEN
#ifdef NANODBC_USE_UNICODE
#ifdef NANODBC_USE_IODBC_WIDE_STRINGS
#define NANODBC_TEXT(s) U##s
typedef std::u32string StringType;
#else
#ifdef _MSC_VER
typedef std::wstring StringType;
#define NANODBC_TEXT(s) L##s
#else
typedef std::u16string StringType;
#define NANODBC_TEXT(s) u##s
#endif
#endif
#else
typedef std::string StringType;
#define NANODBC_TEXT(s) s
#endif

#if defined(_WIN64)
// LLP64 machine: Windows
typedef std::int64_t NullType;
#elif !defined(_WIN64) && defined(__LP64__)
// LP64 machine: OS X or Linux
typedef long NullType;
#else
// 32-bit machine
typedef long NullType;
#endif
#else
/// \def NANODBC_TEXT(s)
/// \brief Creates a string literal of the type corresponding to `Nanodbc::StringType`.
///
/// By default, the macro maps to an unprefixed string literal.
/// If building with options NANODBC_USE_UNICODE=ON and
/// NANODBC_USE_IODBC_WIDE_STRINGS=ON specified, then it prefixes a literal with U"...".
/// If only NANODBC_USE_UNICODE=ON is specified, then:
///   * If building with Visual Studio, then the macro prefixes a literal with L"...".
///   * Otherwise, it prefixes a literal with u"...".
#define NANODBC_TEXT(s) s

/// \c StringType will be \c std::u16string or \c std::32string if \c NANODBC_USE_UNICODE defined.
///
/// Otherwise it will be \c std::string.
typedef unspecified - type StringType;
/// \c NullType will be \c int64_t for 64-bit compilations, otherwise \c long.
typedef unspecified - type NullType;
#endif

#if defined(_MSC_VER) && _MSC_VER <= 1800
// These versions of Visual C++ do not yet support \c noexcept or \c std::move.
#define NANODBC_NOEXCEPT
#define NANODBC_NO_MOVE_CTOR
#else
#define NANODBC_NOEXCEPT noexcept
#endif

// clang-format off
// 8888888888                                      888    888                        888 888 d8b
// 888                                             888    888                        888 888 Y8P
// 888                                             888    888                        888 888
// 8888888    888d888 888d888 .d88b.  888d888      8888888888  8888b.  88888b.   .d88888 888 888 88888b.   .d88b.
// 888        888P"   888P"  d88""88b 888P"        888    888     "88b 888 "88b d88" 888 888 888 888 "88b d88P"88b
// 888        888     888    888  888 888          888    888 .d888888 888  888 888  888 888 888 888  888 888  888
// 888        888     888    Y88..88P 888          888    888 888  888 888  888 Y88b 888 888 888 888  888 Y88b 888
// 8888888888 888     888     "Y88P"  888          888    888 "Y888888 888  888  "Y88888 888 888 888  888  "Y88888
//                                                                                                             888
//                                                                                                        Y8b d88P
//                                                                                                         "Y88P"
// MARK: Error Handling -
// clang-format on

/// \addtogroup exceptions Exception types
/// \brief Possible error conditions.
///
/// Specific errors such as \c TypeIncompatipleError, \c NullAccessError, and
/// \c IndexRangeError can arise from improper use of the Nanodbc library. The general
/// \c DatabaseError is for all other situations in which the ODBC Driver or C API reports an error
/// condition. The explanatory string for DatabaseError will, if possible, contain a diagnostic
/// message obtained from \c SQLGetDiagRec().
/// @{

/// \brief Type incompatible.
/// \see exceptions
class TypeIncompatipleError : public std::runtime_error
{
public:
    TypeIncompatipleError();
    const char* what() const NANODBC_NOEXCEPT;
};

/// \brief Accessed null data.
/// \see exceptions
class NullAccessError : public std::runtime_error
{
public:
    NullAccessError();
    const char* what() const NANODBC_NOEXCEPT;
};

/// \brief Index out of range.
/// \see exceptions
class IndexRangeError : public std::runtime_error
{
public:
    IndexRangeError();
    const char* what() const NANODBC_NOEXCEPT;
};

/// \brief Programming logic error.
/// \see exceptions
class ProgrammingError : public std::runtime_error
{
public:
    explicit ProgrammingError(const std::string& info);
    const char* what() const NANODBC_NOEXCEPT;
};

/// \brief General database error.
/// \see exceptions
class DatabaseError : public std::runtime_error
{
public:
    /// \brief Creates runtime_error with message about last ODBC error.
    /// \param handle The native ODBC Statement or Connection handle.
    /// \param handle_type The native ODBC handle type code for the given handle.
    /// \param info Additional info that will be appended to the beginning of the error message.
    DatabaseError(void* handle, short handle_type, const std::string& info = "");
    const char* what() const NANODBC_NOEXCEPT;
    const long native() const NANODBC_NOEXCEPT;
    const std::string state() const NANODBC_NOEXCEPT;

private:
    long nativeError;
    std::string sqlState;
    std::string message;
};

/// @}

// clang-format off
// 888     888 888    d8b 888 d8b 888    d8b
// 888     888 888    Y8P 888 Y8P 888    Y8P
// 888     888 888        888     888
// 888     888 888888 888 888 888 888888 888  .d88b.  .d8888b
// 888     888 888    888 888 888 888    888 d8P  Y8b 88K
// 888     888 888    888 888 888 888    888 88888888 "Y8888b.
// Y88b. .d88P Y88b.  888 888 888 Y88b.  888 Y8b.          X88
//  "Y88888P"   "Y888 888 888 888  "Y888 888  "Y8888   88888P'
// MARK: Utilities -
// clang-format on

/// \addtogroup utility Utilities
/// \brief Additional Nanodbc utility classes and functions.
///
/// \{

/// \brief A type for representing date data.
struct date
{
    std::int16_t year;  ///< Year [0-inf).
    std::int16_t month; ///< Month of the year [1-12].
    std::int16_t day;   ///< Day of the month [1-31].
};

/// \brief A type for representing time data.
struct time
{
    std::int16_t hour; ///< Hours since midnight [0-23].
    std::int16_t min;  ///< Minutes after the hour [0-59].
    std::int16_t sec;  ///< Seconds after the minute.
};

/// \brief A type for representing timestamp data.
struct timestamp
{
    std::int16_t year;  ///< Year [0-inf).
    std::int16_t month; ///< Month of the year [1-12].
    std::int16_t day;   ///< Day of the month [1-31].
    std::int16_t hour;  ///< Hours since midnight [0-23].
    std::int16_t min;   ///< Minutes after the hour [0-59].
    std::int16_t sec;   ///< Seconds after the minute.
    std::int32_t fract; ///< Fractional seconds.
};

/// \}

/// \addtogroup mainc Main classes
/// \brief Main Nanodbc classes.
///
/// @{

// clang-format off
// 88888888888                                                  888    d8b
//     888                                                      888    Y8P
//     888                                                      888
//     888  888d888 8888b.  88888b.  .d8888b   8888b.   .d8888b 888888 888  .d88b.  88888b.
//     888  888P"      "88b 888 "88b 88K          "88b d88P"    888    888 d88""88b 888 "88b
//     888  888    .d888888 888  888 "Y8888b. .d888888 888      888    888 888  888 888  888
//     888  888    888  888 888  888      X88 888  888 Y88b.    Y88b.  888 Y88..88P 888  888
//     888  888    "Y888888 888  888  88888P' "Y888888  "Y8888P  "Y888 888  "Y88P"  888  888
// MARK: Transaction -
// clang-format on

/// \brief A resource for managing Transaction commits and rollbacks.
/// \attention You will want to use transactions if you are doing batch operations because it will
///            prevent auto commits from occurring after each individual operation is executed.
class Transaction
{
public:
    /// \brief Begin a Transaction on the given Connection object.
    /// \post Operations that modify the database must now be committed before taking effect.
    /// \throws DatabaseError
    explicit Transaction(const class Connection& conn);

    /// Copy constructor.
    Transaction(const Transaction& rhs);

#ifndef NANODBC_NO_MOVE_CTOR
    /// Move constructor.
    Transaction(Transaction&& rhs) NANODBC_NOEXCEPT;
#endif

    /// Assignment.
    Transaction& operator=(Transaction rhs);

    /// Member swap.
    void swap(Transaction& rhs) NANODBC_NOEXCEPT;

    /// \brief If this Transaction has not been committed, will will rollback any modifying ops.
    ~Transaction() NANODBC_NOEXCEPT;

    /// \brief Commits Transaction immediately.
    /// \throws DatabaseError
    void commit();

    /// \brief Marks this Transaction for rollback.
    void rollback() NANODBC_NOEXCEPT;

    /// Returns the Connection object.
    class Connection& Connection();

    /// Returns the Connection object.
    const class Connection& Connection() const;

    /// Returns the Connection object.
    operator class Connection&();

    /// Returns the Connection object.
    operator const class Connection&() const;

private:
    class TransactionImpl;
    friend class Nanodbc::Connection;

private:
    std::shared_ptr<TransactionImpl> impl_;
};

// clang-format off
//  .d8888b.  888             888                                            888
// d88P  Y88b 888             888                                            888
// Y88b.      888             888                                            888
//  "Y888b.   888888  8888b.  888888 .d88b.  88888b.d88b.   .d88b.  88888b.  888888
//     "Y88b. 888        "88b 888   d8P  Y8b 888 "888 "88b d8P  Y8b 888 "88b 888
//       "888 888    .d888888 888   88888888 888  888  888 88888888 888  888 888
// Y88b  d88P Y88b.  888  888 Y88b. Y8b.     888  888  888 Y8b.     888  888 Y88b.
//  "Y8888P"   "Y888 "Y888888  "Y888 "Y8888  888  888  888  "Y8888  888  888  "Y888
// MARK: Statement -
// clang-format on

/// \brief Represents a Statement on the database.
class Statement
{
public:
    /// \brief Provides support for retrieving output/return parameters.
    /// \see binding
    enum ParamDirection
    {
        PARAM_IN,    ///< Binding an input parameter.
        PARAM_OUT,   ///< Binding an output parameter.
        PARAM_INOUT, ///< Binding an input/output parameter.
        PARAM_RETURN ///< Binding a return parameter.
    };

public:
    /// \brief Creates a new un-prepared Statement.
    /// \see execute(), justExecute(), executeDirect(), justExecuteDirect(), open(), prepare()
    Statement();

    /// \brief Constructs a Statement object and associates it to the given Connection.
    /// \param conn The Connection to use.
    /// \see open(), prepare()
    explicit Statement(class Connection& conn);

    /// \brief Constructs and prepares a Statement using the given Connection and query.
    /// \param conn The Connection to use.
    /// \param query The SQL query Statement.
    /// \param timeout The number in seconds before query timeout. Default: 0 meaning no timeout.
    /// \see execute(), justExecute(), executeDirect(), justExecuteDirect(), open(), prepare()
    Statement(class Connection& conn, const StringType& query, long timeout = 0);

    /// \brief Copy constructor.
    Statement(const Statement& rhs);

#ifndef NANODBC_NO_MOVE_CTOR
    /// \brief Move constructor.
    Statement(Statement&& rhs) NANODBC_NOEXCEPT;
#endif

    /// \brief Assignment.
    Statement& operator=(Statement rhs);

    /// \brief Member swap.
    void swap(Statement& rhs) NANODBC_NOEXCEPT;

    /// \brief Closes the Statement.
    /// \see close()
    ~Statement() NANODBC_NOEXCEPT;

    /// \brief Creates a Statement for the given Connection.
    /// \param conn The Connection where the Statement will be executed.
    /// \throws DatabaseError
    void open(class Connection& conn);

    /// \brief Returns true if Connection is open.
    bool open() const;

    /// \brief Returns true if connected to the database.
    bool connected() const;

    /// \brief Returns the associated Connection object if any.
    class Connection& Connection();

    /// \brief Returns the associated Connection object if any.
    const class Connection& Connection() const;

    /// \brief Returns the native ODBC Statement handle.
    void* nativeStatementHandle() const;

    /// \brief Closes the Statement and frees all associated resources.
    void close();

    /// \brief Cancels execution of the Statement.
    /// \throws DatabaseError
    void cancel();

    /// \brief Opens and prepares the given Statement to execute on the given Connection.
    /// \param conn The Connection where the Statement will be executed.
    /// \param query The SQL query that will be executed.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \see open()
    /// \throws DatabaseError
    void prepare(class Connection& conn, const StringType& query, long timeout = 0);

    /// \brief Prepares the given Statement to execute its associated Connection.
    /// \note If the Statement is not open throws ProgrammingError.
    /// \param query The SQL query that will be executed.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \see open()
    /// \throws DatabaseError, ProgrammingError
    void prepare(const StringType& query, long timeout = 0);

    /// \brief Sets the number in seconds before query timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    void timeout(long timeout = 0);

    /// \brief Opens, prepares, and executes the given query directly on the given Connection.
    /// \param conn The Connection where the Statement will be executed.
    /// \param query The SQL query that will be executed.
    /// \param batchOperations Numbers of rows to fetch per rowset, or the number of batch
    ///        parameters to process.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \return A Result set object.
    /// \attention You will want to use transactions if you are doing batch operations because it
    ///            will prevent auto commits occurring after each individual operation is executed.
    /// \see open(), prepare(), execute(), Result, Transaction
    class Result executeDirect(
        class Connection& conn,
        const StringType& query,
        long batchOperations = 1,
        long timeout = 0);

#if !defined(NANODBC_DISABLE_ASYNC)
    /// \brief Prepare the given Statement, in asynchronous mode.
    /// \note If the Statement is not open throws ProgrammingError.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_STMT_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entirely by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \param eventHandle The event handle the caller will wait before calling completePrepare.
    /// \param query The SQL query that will be prepared.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \throws DatabaseError
    /// \return Boolean: true if the event handle needs to be awaited, false is Result is ready now.
    /// \see completePrepare()
    bool asyncPrepare(const StringType& query, void* eventHandle, long timeout = 0);

    /// \brief Completes a previously initiated asynchronous query preparation.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_STMT_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entirely by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \throws DatabaseError
    /// \see asyncPrepare()
    void completePrepare();

    /// \brief Opens, prepares, and executes query directly on the given Connection, in async mode.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_STMT_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entirely by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \param conn The Connection where the Statement will be executed.
    /// \param eventHandle The event handle the caller will wait before calling complete_execute.
    /// \param query The SQL query that will be executed.
    /// \param batchOperations Rows to fetch per rowset or number of batch parameters to process.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \throws DatabaseError
    /// \return Boolean: true if event handle needs to be awaited, false if Result ready now.
    /// \attention You will want to use transactions if you are doing batch operations because it
    ///            will prevent auto commits after each individual operation is executed.
    /// \see complete_execute(), open(), prepare(), execute(), Result, Transaction
    bool asyncExecuteDirect(
        class Connection& conn,
        void* eventHandle,
        const StringType& query,
        long batchOperations = 1,
        long timeout = 0);

    /// \brief Execute the previously prepared query now, in asynchronous mode.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_STMT_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entirely by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \param eventHandle The event handle the caller will wait before calling complete_execute.
    /// \param batchOperations Rows to fetch per rowset or number of batch parameters to process.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \throws DatabaseError
    /// \return Boolean: true if event handle needs to be awaited, false if Result is ready now.
    /// \attention You will want to use transactions if you are doing batch operations because it
    ///            will prevent auto commits after each individual operation is executed.
    /// \see complete_execute(), open(), prepare(), Result, Transaction
    bool async_execute(void* eventHandle, long batchOperations = 1, long timeout = 0);

    /// \brief Completes a previously initiated asynchronous query execution, returning the Result.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_STMT_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entirely by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \throws DatabaseError
    /// \return A Result set object.
    /// \param batchOperations Rows to fetch per rowset or number of batch parameters to process.
    /// \see async_execute(), asyncExecuteDirect()
    class Result complete_execute(long batchOperations = 1);

    /// left for backwards compatibility
    class Result asyncComplete(long batchOperations = 1);

    /// undocumented - for internal use only (used from ResultImpl)
    void enableAsync(void* eventHandle);

    /// undocumented - for internal use only (used from ResultImpl)
    void disableAsync() const;
#endif

    /// \brief Execute the previously prepared query now without constructing Result object.
    /// \param conn The Connection where the Statement will be executed.
    /// \param query The SQL query that will be executed.
    /// \param batchOperations Rows to fetch per rowset, or number of batch parameters to process.
    /// \param timeout Seconds before query timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    /// \return A Result set object.
    /// \attention You will want to use transactions if you are doing batch operations because it
    ///            will prevent auto commits after each individual operation is executed.
    /// \see open(), prepare(), execute(), executeDirect(), Result, Transaction
    void justExecuteDirect(
        class Connection& conn,
        const StringType& query,
        long batchOperations = 1,
        long timeout = 0);

    /// \brief Execute the previously prepared query now.
    /// \param batchOperations Rows to fetch per rowset, or number of batch parameters to process.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \throws DatabaseError
    /// \return A Result set object.
    /// \attention You will want to use transactions if you are doing batch operations because it
    ///            will prevent auto commits after each individual operation is executed.
    /// \see open(), prepare(), Result, Transaction
    class Result execute(long batchOperations = 1, long timeout = 0);

    /// \brief Execute the previously prepared query now without constructing Result object.
    /// \param batchOperations Rows to fetch per rowset, or number of batch parameters to process.
    /// \param timeout The number in seconds before query timeout. Default 0 meaning no timeout.
    /// \throws DatabaseError
    /// \return A Result set object.
    /// \attention You will want to use transactions if you are doing batch operations because it
    ///            will prevent auto commits after each individual operation is executed.
    /// \see open(), prepare(), execute(), Result, Transaction
    void justExecute(long batchOperations = 1, long timeout = 0);

    /// \brief Returns the input and output paramters of the specified stored procedure.
    /// \param Catalog The Catalog name of the procedure.
    /// \param schema Pattern to use for schema names.
    /// \param procedure The name of the procedure.
    /// \param column Pattern to use for column names.
    /// \throws DatabaseError
    /// \return A Result set object.
    class Result procedure_columns(
        const StringType& Catalog,
        const StringType& schema,
        const StringType& procedure,
        const StringType& column);

    /// \brief Returns rows affected by the request or -1 if affected rows is not available.
    /// \throws DatabaseError
    long affectedRows() const;

    /// \brief Returns the number of columns in a Result set.
    /// \throws DatabaseError
    short columns() const;

    /// \brief Resets all currently bound parameters.
    void resetParameters() NANODBC_NOEXCEPT;

    /// \brief Returns parameter size for indicated parameter placeholder in a prepared Statement.
    unsigned long parameterSize(short param_index) const;

    /// \addtogroup binding Binding parameters
    /// \brief These functions are used to bind values to ODBC parameters.
    ///
    /// @{

    /// \brief Binds given value to given parameter placeholder number in the prepared Statement.
    ///
    /// If your prepared SQL query has any ? placeholders, this is how you bind values to them.
    /// Placeholder numbers count from left to right and are 0-indexed.
    ///
    /// It is NOT possible to use these functions for batch operations as number of elements is not
    /// specified here.
    ///
    /// \param param_index Zero-based index of parameter marker (placeholder position).
    /// \param value Value to substitute into placeholder.
    /// \param direction ODBC parameter direction.
    /// \throws DatabaseError
    template <class T>
    void bind(short param_index, T const* value, ParamDirection direction = PARAM_IN);

    /// \addtogroup bind_multi Binding multiple non-string values
    /// \brief Binds given values to given parameter placeholder number in the prepared Statement.
    ///
    /// If your prepared SQL query has any parameter markers, ? (question  mark) placeholders,
    /// this is how you bind values to them.
    /// Parameter markers are numbered using Zero-based index from left to right.
    ///
    /// It is possible to use these functions for batch operations.
    ///
    /// \param param_index Zero-based index of parameter marker (placeholder position).
    /// \param values Values to substitute into placeholder.
    /// \param batch_size The number of values being bound.
    /// \param nullSentry Value which should represent a null value.
    /// \param nulls Flags for values that should be set to a null value.
    /// \param param_direciton ODBC parameter direction.
    /// \throws DatabaseError
    ///
    /// @{

    /// \brief Binds multiple values.
    /// \see bind_multi
    template <class T>
    void bind(
        short param_index,
        T const* values,
        std::size_t batch_size,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple values.
    /// \see bind_multi
    template <class T>
    void bind(
        short param_index,
        T const* values,
        std::size_t batch_size,
        T const* nullSentry,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple values.
    /// \see bind_multi
    template <class T>
    void bind(
        short param_index,
        T const* values,
        std::size_t batch_size,
        bool const* nulls,
        ParamDirection direction = PARAM_IN);

    /// @}

    /// \addtogroup bindStrings Binding multiple string values
    /// \brief Binds given string values to parameter marker in prepared Statement.
    ///
    /// If your prepared SQL query has any parameter markers, ? (question  mark) placeholders,
    /// this is how you bind values to them.
    /// Parameter markers are numbered using Zero-based index from left to right.
    ///
    /// It is possible to use these functions for batch operations.
    ///
    /// \param param_index Zero-based index of parameter marker (placeholder position).
    /// \param values Array of values to substitute into parameter placeholders.
    /// \param value_size Maximum length of string value in array.
    /// \param batch_size Number of string values to bind. Otherwise template parameter BatchSize is
    /// taken as the number of values.
    /// \param nullSentry Value which should represent a null value.
    /// \param nulls Flags for values that should be set to a null value.
    /// \param param_direciton ODBC parameter direction.
    /// \throws DatabaseError
    ///
    /// @{

    /// \brief Binds multiple string values.
    /// \see bindStrings
    void bindStrings(
        short param_index,
        StringType::value_type const* values,
        std::size_t value_size,
        std::size_t batch_size,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple string values.
    ///
    /// Size of the values vector indicates number of values to bind.
    /// Longest string in the array determines maximum length of individual value.
    ///
    /// \see bindStrings
    void bindStrings(
        short param_index,
        std::vector<StringType> const& values,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple string values.
    /// \see bindStrings
    template <std::size_t BatchSize, std::size_t ValueSize>
    void bindStrings(
        short param_index,
        StringType::value_type const (&values)[BatchSize][ValueSize],
        ParamDirection direction = PARAM_IN)
    {
        auto param_values = reinterpret_cast<StringType::value_type const*>(values);
        bindStrings(param_index, param_values, ValueSize, BatchSize, direction);
    }

    /// \brief Binds multiple string values.
    /// \see bindStrings
    void bindStrings(
        short param_index,
        StringType::value_type const* values,
        std::size_t value_size,
        std::size_t batch_size,
        StringType::value_type const* nullSentry,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple string values.
    /// \see bindStrings
    void bindStrings(
        short param_index,
        std::vector<StringType> const& values,
        StringType::value_type const* nullSentry,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple string values.
    /// \see bindStrings
    template <std::size_t BatchSize, std::size_t ValueSize>
    void bindStrings(
        short param_index,
        StringType::value_type const (&values)[BatchSize][ValueSize],
        StringType::value_type const* nullSentry,
        ParamDirection direction = PARAM_IN)
    {
        auto param_values = reinterpret_cast<StringType::value_type const*>(values);
        bindStrings(param_index, param_values, ValueSize, BatchSize, nullSentry, direction);
    }

    /// \brief Binds multiple string values.
    /// \see bindStrings
    void bindStrings(
        short param_index,
        StringType::value_type const* values,
        std::size_t value_size,
        std::size_t batch_size,
        bool const* nulls,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple string values.
    /// \see bindStrings
    void bindStrings(
        short param_index,
        std::vector<StringType> const& values,
        bool const* nulls,
        ParamDirection direction = PARAM_IN);

    /// \brief Binds multiple string values.
    /// \see bindStrings
    template <std::size_t BatchSize, std::size_t ValueSize>
    void bindStrings(
        short param_index,
        StringType::value_type const (&values)[BatchSize][ValueSize],
        bool const* nulls,
        ParamDirection direction = PARAM_IN)
    {
        auto param_values = reinterpret_cast<StringType::value_type const*>(values);
        bindStrings(param_index, param_values, ValueSize, BatchSize, nulls, direction);
    }

    /// @}

    /// \brief Binds null values to the parameter placeholder number in the prepared Statement.
    ///
    /// If your prepared SQL query has any parameter markers, ? (question  mark) placeholders,
    /// this is how you bind values to them.
    /// Parameter markers are numbered using Zero-based index from left to right.
    ///
    /// It is possible to use this function for batch operations.
    ///
    /// \param param_index Zero-based index of parameter marker (placeholder position).
    /// \param batch_size The number of elements being bound.
    /// \throws DatabaseError
    void bind_null(short param_index, std::size_t batch_size = 1);

    /// @}

private:
    typedef std::function<bool(std::size_t)> NullPredicateType;

private:
    class StatementImpl;
    friend class Nanodbc::Result;

private:
    std::shared_ptr<StatementImpl> impl_;
};

// clang-format off
//  .d8888b.                                               888    d8b
// d88P  Y88b                                              888    Y8P
// 888    888                                              888
// 888         .d88b.  88888b.  88888b.   .d88b.   .d8888b 888888 888  .d88b.  88888b.
// 888        d88""88b 888 "88b 888 "88b d8P  Y8b d88P"    888    888 d88""88b 888 "88b
// 888    888 888  888 888  888 888  888 88888888 888      888    888 888  888 888  888
// Y88b  d88P Y88..88P 888  888 888  888 Y8b.     Y88b.    Y88b.  888 Y88..88P 888  888
//  "Y8888P"   "Y88P"  888  888 888  888  "Y8888   "Y8888P  "Y888 888  "Y88P"  888  888
// MARK: Connection -
// clang-format on

/// \brief Manages and encapsulates ODBC resources such as the Connection and environment handles.
class Connection
{
public:
    /// \brief Create new Connection object, initially not connected.
    Connection();

    /// Copy constructor.
    Connection(const Connection& rhs);

#ifndef NANODBC_NO_MOVE_CTOR
    /// Move constructor.
    Connection(Connection&& rhs) NANODBC_NOEXCEPT;
#endif

    /// Assignment.
    Connection& operator=(Connection rhs);

    /// Member swap.
    void swap(Connection&) NANODBC_NOEXCEPT;

    /// \brief Create new Connection object and immediately connect to the given data source.
    /// \param dsn The name of the data source.
    /// \param user The username for authenticating to the data source.
    /// \param pass The password for authenticating to the data source.
    /// \param timeout Seconds before Connection timeout. Default 0 meaning no timeout.
    /// \throws DatabaseError
    /// \see connected(), connect()
    Connection(
        const StringType& dsn,
        const StringType& user,
        const StringType& pass,
        long timeout = 0);

    /// \brief Create new Connection object and immediately connect using the given Connection
    /// string.
    /// \param connectionString The Connection string for establishing a Connection.
    /// \param timeout Seconds before Connection timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    /// \see connected(), connect()
    Connection(const StringType& connectionString, long timeout = 0);

    /// \brief Automatically disconnects from the database and frees all associated resources.
    ///
    /// Will not throw even if disconnecting causes some kind of error and raises an exception.
    /// If you explicitly need to know if disconnect() succeeds, call it directly.
    ~Connection() NANODBC_NOEXCEPT;

    /// \brief Connect to the given data source.
    /// \param dsn The name of the data source.
    /// \param user The username for authenticating to the data source.
    /// \param pass The password for authenticating to the data source.
    /// \param timeout Seconds before Connection timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    /// \see connected()
    void connect(
        const StringType& dsn,
        const StringType& user,
        const StringType& pass,
        long timeout = 0);

    /// \brief Connect using the given Connection string.
    /// \param connectionString The Connection string for establishing a Connection.
    /// \param timeout Seconds before Connection timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    /// \see connected()
    void connect(const StringType& connectionString, long timeout = 0);

#if !defined(NANODBC_DISABLE_ASYNC)
    /// \brief Initiate an asynchronous Connection operation to the given data source.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_DBC_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entierly by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \param dsn The name of the data source.
    /// \param user The username for authenticating to the data source.
    /// \param pass The password for authenticating to the data source.
    /// \param eventHandle The event handle the caller will wait before calling asyncComplete.
    /// \param timeout Seconds before Connection timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    /// \return Boolean: true if event handle needs to be awaited, false if Connection is ready now.
    /// \see connected()
    bool async_connect(
        const StringType& dsn,
        const StringType& user,
        const StringType& pass,
        void* eventHandle,
        long timeout = 0);

    /// \brief Initiate an asynchronous Connection operation using the given Connection string.
    ///
    /// This method will only be available if Nanodbc is built against ODBC headers and library that
    /// supports asynchronous mode. Such that the identifiers `SQL_ATTR_ASYNC_DBC_EVENT` and
    /// `SQLCompleteAsync` are extant. Otherwise this method will be defined, but not implemented.
    ///
    /// Asynchronous features can be disabled entierly by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    ///
    /// \param connectionString The Connection string for establishing a Connection.
    /// \param eventHandle Event handle the caller will wait before calling asyncComplete.
    /// \param timeout Seconds before Connection timeout. Default is 0 indicating no timeout.
    /// \throws DatabaseError
    /// \return Boolean: true if event handle needs to be awaited, false if Connection is ready now.
    /// \see connected()
    bool async_connect(const StringType& connectionString, void* eventHandle, long timeout = 0);

    /// \brief Completes a previously initiated asynchronous Connection operation.
    ///
    /// Asynchronous features can be disabled entierly by defining `NANODBC_DISABLE_ASYNC` when
    /// building Nanodbc.
    void asyncComplete();
#endif

    /// \brief Returns true if connected to the database.
    bool connected() const;

    /// \brief Disconnects from the database, but maintains environment and handle resources.
    void disconnect();

    /// \brief Returns the number of transactions currently held for this Connection.
    std::size_t transactions() const;

    /// \brief Returns the native ODBC database Connection handle.
    void* nativeDbcHandle() const;

    /// \brief Returns the native ODBC environment handle.
    void* nativeEnvHandle() const;

    /// \brief Returns information from the ODBC Connection as a string.
    template <class T>
    T get_info(short info_type) const;

    /// \brief Returns name of the DBMS product.
    /// Returns the ODBC information type SQL_DBMS_NAME of the DBMS product
    /// accesssed by the Driver via the current Connection.
    StringType dbmsName() const;

    /// \brief Returns version of the DBMS product.
    /// Returns the ODBC information type SQL_DBMS_VER of the DBMS product
    /// accesssed by the Driver via the current Connection.
    StringType dbmsVersion() const;

    /// \brief Returns the name of the ODBC Driver.
    /// \throws DatabaseError
    StringType driverName() const;

    /// \brief Returns the name of the currently connected database.
    /// Returns the current SQL_DATABASE_NAME information value associated with the Connection.
    StringType databaseName() const;

    /// \brief Returns the name of the current Catalog.
    /// Returns the current setting of the Connection Attribute SQL_ATTR_CURRENT_CATALOG.
    StringType catalogName() const;

private:
    std::size_t refTransaction();
    std::size_t unrefTransaction();
    bool rollback() const;
    void rollback(bool onoff);

private:
    class ConnectionImpl;
    friend class Nanodbc::Transaction::TransactionImpl;

private:
    std::shared_ptr<ConnectionImpl> impl_;
};

// clang-format off
// 8888888b.                            888 888
// 888   Y88b                           888 888
// 888    888                           888 888
// 888   d88P .d88b.  .d8888b  888  888 888 888888
// 8888888P" d8P  Y8b 88K      888  888 888 888
// 888 T88b  88888888 "Y8888b. 888  888 888 888
// 888  T88b Y8b.          X88 Y88b 888 888 Y88b.
// 888   T88b "Y8888   88888P'  "Y88888 888  "Y888
// MARK: Result -
// clang-format on

class Catalog;

/// \brief A resource for managing Result sets from Statement execution.
///
/// \see Statement::execute(), Statement::executeDirect()
/// \note Result objects may be copied, however all copies will refer to the same Result set.
class Result
{
public:
    /// \brief Empty Result set.
    Result();

    /// \brief Free Result set.
    ~Result() NANODBC_NOEXCEPT;

    /// \brief Copy constructor.
    Result(const Result& rhs);

#ifndef NANODBC_NO_MOVE_CTOR
    /// \brief Move constructor.
    Result(Result&& rhs) NANODBC_NOEXCEPT;
#endif

    /// \brief Assignment.
    Result& operator=(Result rhs);

    /// \brief Member swap.
    void swap(Result& rhs) NANODBC_NOEXCEPT;

    /// \brief Returns the native ODBC Statement handle.
    void* nativeStatementHandle() const;

    /// \brief The rowset size for this Result set.
    long rowsetSize() const NANODBC_NOEXCEPT;

    /// \brief Number of affected rows by the request or -1 if the affected rows is not available.
    /// \throws DatabaseError
    long affectedRows() const;

    /// \brief Rows in the current rowset or 0 if the number of rows is not available.
    long rows() const NANODBC_NOEXCEPT;

    /// \brief Returns the number of columns in a Result set.
    /// \throws DatabaseError
    short columns() const;

    /// \brief Fetches the first row in the current Result set.
    /// \return true if there are more results or false otherwise.
    /// \throws DatabaseError
    bool first();

    /// \brief Fetches the last row in the current Result set.
    /// \return true if there are more results or false otherwise.
    /// \throws DatabaseError
    bool last();

    /// \brief Fetches the next row in the current Result set.
    /// \return true if there are more results or false otherwise.
    /// \throws DatabaseError
    bool next();

#if !defined(NANODBC_DISABLE_ASYNC)
    /// \brief Initiates an asynchronous fetch of the next row in the current Result set.
    /// \return true if the caller needs to wait for the event to be signalled, false if
    ///         completeNext() can be called immediately.
    /// \throws DatabaseError
    bool asyncNext(void* eventHandle);

    /// \brief Completes a previously-initiated async fetch for next row in the current Result set.
    /// \return true if there are more results or false otherwise.
    /// \throws DatabaseError
    bool completeNext();
#endif

    /// \brief Fetches the prior row in the current Result set.
    /// \return true if there are more results or false otherwise.
    /// \throws DatabaseError
    bool prior();

    /// \brief Moves to and fetches the specified row in the current Result set.
    /// \return true if there are results or false otherwise.
    /// \throws DatabaseError
    bool move(long row);

    /// \brief Skips a number of rows and then fetches the resulting row in the current Result set.
    /// \return true if there are results or false otherwise.
    /// \throws DatabaseError
    bool skip(long rows);

    /// \brief Returns the row position in the current Result set.
    unsigned long position() const;

    /// \brief Returns true if there are no more results in the current Result set.
    bool atEnd() const NANODBC_NOEXCEPT;

    /// \brief Gets data from the given column of the current rowset.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \param Result The column's value will be written to this parameter.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError, NullAccessError
    template <class T>
    void getRef(short column, T& Result) const;

    /// \brief Gets data from the given column of the current rowset.
    ///
    /// If the data is null, fallback is returned instead.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \param fallback if value is null, return fallback instead.
    /// \param Result The column's value will be written to this parameter.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError
    template <class T>
    void getRef(short column, const T& fallback, T& Result) const;

    /// \brief Gets data from the given column by name of the current rowset.
    ///
    /// \param columnName column's name.
    /// \param Result The column's value will be written to this parameter.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError, NullAccessError
    template <class T>
    void getRef(const StringType& columnName, T& Result) const;

    /// \brief Gets data from the given column by name of the current rowset.
    ///
    /// If the data is null, fallback is returned instead.
    ///
    /// \param columnName column's name.
    /// \param fallback if value is null, return fallback instead.
    /// \param Result The column's value will be written to this parameter.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError
    template <class T>
    void getRef(const StringType& columnName, const T& fallback, T& Result) const;

    /// \brief Gets data from the given column of the current rowset.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError, NullAccessError
    template <class T>
    T get(short column) const;

    /// \brief Gets data from the given column of the current rowset.
    ///
    /// If the data is null, fallback is returned instead.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \param fallback if value is null, return fallback instead.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError
    template <class T>
    T get(short column, const T& fallback) const;

    /// \brief Gets data from the given column by name of the current rowset.
    ///
    /// \param columnName column's name.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError, NullAccessError
    template <class T>
    T get(const StringType& columnName) const;

    /// \brief Gets data from the given column by name of the current rowset.
    ///
    /// If the data is null, fallback is returned instead.
    ///
    /// \param columnName column's name.
    /// \param fallback if value is null, return fallback instead.
    /// \throws DatabaseError, IndexRangeError, TypeIncompatipleError
    template <class T>
    T get(const StringType& columnName, const T& fallback) const;

    /// \brief Returns true if and only if the given column of the current rowset is null.
    ///
    /// There is a bug/limitation in ODBC drivers for SQL Server (and possibly others)
    /// which causes SQLBindCol() to never write SQL_NOT_NULL to the length/indicator
    /// buffer unless you also bind the data column. Nanodbc's isNull() will return
    /// correct values for (n)varchar(max) columns when you ensure that SQLGetData()
    /// has been called for that column (i.e. after get() or getRef() is called).
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \see get(), getRef()
    /// \param column position.
    /// \throws DatabaseError, IndexRangeError
    bool isNull(short column) const;

    /// \brief Returns true if and only if the given column by name of the current rowset is null.
    ///
    /// See isNull(short column) for details on a bug/limitation of some ODBC drivers.
    /// \see isNull()
    /// \param columnName column's name.
    /// \throws DatabaseError, IndexRangeError
    bool isNull(const StringType& columnName) const;

    /// \brief Returns the column number of the specified column name.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param columnName column's name.
    /// \throws IndexRangeError
    short column(const StringType& columnName) const;

    /// \brief Returns the name of the specified column.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \throws IndexRangeError
    StringType columnName(short column) const;

    /// \brief Returns the size of the specified column.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \throws IndexRangeError
    long columnSize(short column) const;

    /// \brief Returns the size of the specified column by name.
    long columnSize(const StringType& columnName) const;

    /// \brief Returns the number of decimal digits of the specified column.
    ///
    /// Applies to exact numeric types (scale), datetime and interval types (prcision).
    /// If the number cannot be determined or is not applicable, drivers typically return 0.
    ///
    /// Columns are numbered from left to right and 0-indexed.
    /// \param column position.
    /// \throws IndexRangeError
    int columnDecimalDigits(short column) const;

    /// \brief Returns the number of decimal digits of the specified column by name.
    int columnDecimalDigits(const StringType& columnName) const;

    /// \brief Returns a identifying integer value representing the SQL type of this column.
    int columnDatatype(short column) const;

    /// \brief Returns a identifying integer value representing the SQL type of this column by name.
    int columnDatatype(const StringType& columnName) const;

    /// \brief Returns a identifying integer value representing the C type of this column.
    int columnCDatatype(short column) const;

    /// \brief Returns a identifying integer value representing the C type of this column by name.
    int columnCDatatype(const StringType& columnName) const;

    /// \brief Returns the next Result, e.g. when stored procedure returns multiple Result sets.
    bool nextResult();

    /// \brief If and only if Result object is valid, returns true.
    explicit operator bool() const;

private:
    Result(Statement Statement, long rowsetSize);

private:
    class ResultImpl;
    friend class Nanodbc::Statement::StatementImpl;
    friend class Nanodbc::Catalog;

private:
    std::shared_ptr<ResultImpl> impl_;
};

/// \brief Single pass input iterator that accesses successive rows in the attached Result set.
class ResultIterator
{
public:
    typedef std::input_iterator_tag IteratorCategory; ///< Category of iterator.
    typedef Result value_type;                         ///< Values returned by iterator access.
    typedef Result* pointer;                           ///< Pointer to iteration values.
    typedef Result& reference;                         ///< Reference to iteration values.
    typedef std::ptrdiff_t difference_type;            ///< Iterator difference.

    /// Default iterator; an empty Result set.
    ResultIterator() = default;

    /// Create Result iterator for a given Result set.
    ResultIterator(Result& r)
        : result_(r)
    {
        ++(*this);
    }

    /// Dereference.
    reference operator*() { return result_; }

    /// Access through dereference.
    pointer operator->()
    {
        if (!result_)
            throw std::runtime_error("Result is empty");
        return &(operator*());
    }

    /// Iteration.
    ResultIterator& operator++()
    {
        try
        {
            if (!result_.next())
                result_ = Result();
        }
        catch (...)
        {
            result_ = Result();
        }
        return *this;
    }

    /// Iteration.
    ResultIterator operator++(int)
    {
        ResultIterator tmp(*this);
        ++(*this);
        return tmp;
    }

    /// Iterators are equal if they a tied to the same native statemnt handle, or both empty.
    bool operator==(ResultIterator const& rhs) const
    {
        if (result_ && rhs.result_)
            return result_.nativeStatementHandle() == rhs.result_.nativeStatementHandle();
        else
            return !result_ && !rhs.result_;
    }

    /// Iterators are not equal if they have different native statemnt handles.
    bool operator!=(ResultIterator const& rhs) const { return !(*this == rhs); }

private:
    Result result_;
};

/// \brief Returns an iterator to the beginning of the given Result set.
inline auto begin(Result& r)
{
    return ResultIterator(r);
}

/// \brief Returns an iterator to the end of a Result set.
///
/// The default-constructed `Nanodbc::ResultIterator` is known as the end-of-Result iterator.
/// When a valid `Nanodbc::ResultIterator` reaches the end of the underlying Result set,
/// it becomes equal to the end-of-Result iterator.
/// Dereferencing or incrementing it further is undefined.
inline auto end(Result& /*r*/)
{
    return ResultIterator();
}

// clang-format off
//
//  .d8888b.           888             888
// d88P  Y88b          888             888
// 888    888          888             888
// 888         8888b.  888888  8888b.  888  .d88b.   .d88b.
// 888            "88b 888        "88b 888 d88""88b d88P"88b
// 888    888 .d888888 888    .d888888 888 888  888 888  888
// Y88b  d88P 888  888 Y88b.  888  888 888 Y88..88P Y88b 888
//  "Y8888P"  "Y888888  "Y888 "Y888888 888  "Y88P"   "Y88888
//                                                      888
//                                                 Y8b d88P
//                                                  "Y88P"
// MARK: Catalog -
// clang-format on

/// \brief A resource for get Catalog information from connected data source.
///
/// Queries are performed using the Catalog Functions in ODBC.
/// All provided operations are convenient wrappers around the ODBC API
/// The original ODBC behaviour should not be affected by any added processing.
class Catalog
{
public:
    /// \brief Result set for a list of Tables in the data source.
    class Tables
    {
    public:
        bool next();                       ///< Move to the next Result in the Result set.
        StringType tableCatalog() const; ///< Fetch table Catalog.
        StringType tableSchema() const;  ///< Fetch table schema.
        StringType tableName() const;    ///< Fetch table name.
        StringType tableType() const;    ///< Fetch table type.
        StringType TableRemarks() const; ///< Fetch table remarks.

    private:
        friend class Nanodbc::Catalog;
        Tables(Result& findResult);
        Result result_;
    };

    /// \brief Result set for a list of columns in one or more Tables.
    class columns
    {
    public:
        bool next();                           ///< Move to the next Result in the Result set.
        StringType tableCatalog() const;     ///< Fetch table Catalog.
        StringType tableSchema() const;      ///< Fetch table schema.
        StringType tableName() const;        ///< Fetch table name.
        StringType columnName() const;       ///< Fetch column name.
        short dataType() const;               ///< Fetch column data type.
        StringType typeName() const;         ///< Fetch column type name.
        long columnSize() const;              ///< Fetch column size.
        long bufferLength() const;            ///< Fetch buffer length.
        short decimalDigits() const;          ///< Fetch decimal digits.
        short numericPrecisionRadix() const; ///< Fetch numeric precission.
        short nullable() const;                ///< True iff column is nullable.
        StringType remarks() const;           ///< Fetch column remarks.
        StringType columnDefault() const;    ///< Fetch column's default.
        short sqlDataType() const;           ///< Fetch column's SQL data type.
        short sqlDatetimeSubtype() const;    ///< Fetch datetime subtype of column.
        long charOctetLength() const;        ///< Fetch char octet length.

        /// \brief Ordinal position of the column in the table.
        /// The first column in the table is number 1.
        /// Returns ORDINAL_POSITION column value in Result set returned by SQLColumns.
        long ordinalPosition() const;

        /// \brief Fetch column is-nullable information.
        ///
        /// \note MSDN: This column returns a zero-length string if nullability is unknown.
        ///       ISO rules are followed to determine nullability.
        ///       An ISO SQL-compliant DBMS cannot return an empty string.
        StringType isNullable() const;

    private:
        friend class Nanodbc::Catalog;
        columns(Result& findResult);
        Result result_;
    };

    /// \brief Result set for a list of columns that compose the primary key of a single table.
    class PrimaryKeys
    {
    public:
        bool next();                       ///< Move to the next Result in the Result set.
        StringType tableCatalog() const; ///< Fetch table Catalog.
        StringType tableSchema() const;  ///< Fetch table schema.
        StringType tableName() const;    ///< Fetch table name.
        StringType columnName() const;   ///< Fetch column name.

        /// \brief Column sequence number in the key (starting with 1).
        /// Returns valye of KEY_SEQ column in Result set returned by SQLPrimaryKeys.
        short columnNumber() const;

        /// \brief Primary key name.
        /// NULL if not applicable to the data source.
        /// Returns valye of PK_NAME column in Result set returned by SQLPrimaryKeys.
        StringType primaryKeyName() const;

    private:
        friend class Nanodbc::Catalog;
        PrimaryKeys(Result& findResult);
        Result result_;
    };

    /// \brief Result set for a list of Tables and the privileges associated with each table.
    class TablePrivileges
    {
    public:
        bool next();                       ///< Move to the next Result in the Result set
        StringType tableCatalog() const; ///< Fetch table Catalog.
        StringType tableSchema() const;  ///< Fetch table schema.
        StringType tableName() const;    ///< Fetch table name.
        StringType grantor() const;       ///< Fetch name of user who granted the privilege.
        StringType grantee() const;       ///< Fetch name of user whom the privilege was granted.
        StringType privilege() const;     ///< Fetch the table privilege.
        /// Fetch indicator whether the grantee is permitted to grant the privilege to other users.
        StringType isGrantable() const;

    private:
        friend class Nanodbc::Catalog;
        TablePrivileges(Result& findResult);
        Result result_;
    };

    /// \brief Creates Catalog operating on database accessible through the specified Connection.
    Catalog(Connection& conn);

    /// \brief Creates Result set with catalogs, schemas, Tables, or table types.
    ///
    /// Tables information is obtained by executing `SQLTable` function within
    /// scope of the connected database accessible with the specified Connection.
    /// Since this function is implemented in terms of the `SQLTable`s, it returns
    /// Result set ordered by TABLE_TYPE, TABLE_CAT, TABLE_SCHEM, and TABLE_NAME.
    ///
    /// All arguments are treated as the Pattern Value Arguments.
    /// Empty string argument is equivalent to passing the search pattern '%'.
    Catalog::Tables find_tables(
        const StringType& table = StringType(),
        const StringType& type = StringType(),
        const StringType& schema = StringType(),
        const StringType& Catalog = StringType());

    /// \brief Creates Result set with Tables and the privileges associated with each table.
    /// Tables information is obtained by executing `SQLTablePrivileges` function within
    /// scope of the connected database accessible with the specified Connection.
    /// Since this function is implemented in terms of the `SQLTablePrivileges`s, it returns
    /// Result set ordered by TABLE_CAT, TABLE_SCHEM, TABLE_NAME, PRIVILEGE, and GRANTEE.
    ///
    /// \param Catalog The table Catalog. It cannot contain a string search pattern.
    /// \param schema String search pattern for schema names, treated as the Pattern Value
    /// Arguments.
    /// \param table String search pattern for table names, treated as the Pattern Value Arguments.
    ///
    /// \note Due to the fact Catalog cannot is not the Pattern Value Argument,
    ///       order of parameters is different than in the other Catalog look-up functions.
    Catalog::TablePrivileges findTablePrivileges(
        const StringType& Catalog,
        const StringType& table = StringType(),
        const StringType& schema = StringType());

    /// \brief Creates Result set with columns in one or more Tables.
    ///
    /// Columns information is obtained by executing `SQLColumns` function within
    /// scope of the connected database accessible with the specified Connection.
    /// Since this function is implemented in terms of the `SQLColumns`, it returns
    /// Result set ordered by TABLE_CAT, TABLE_SCHEM, TABLE_NAME, and ORDINAL_POSITION.
    ///
    /// All arguments are treated as the Pattern Value Arguments.
    /// Empty string argument is equivalent to passing the search pattern '%'.
    Catalog::columns findColumns(
        const StringType& column = StringType(),
        const StringType& table = StringType(),
        const StringType& schema = StringType(),
        const StringType& Catalog = StringType());

    /// \brief Creates Result set with columns that compose the primary key of a single table.
    ///
    /// Returns Result set with column names that make up the primary key for a table.
    /// The primary key information is obtained by executing `SQLPrimaryKey` function within
    /// scope of the connected database accessible with the specified Connection.
    ///
    /// All arguments are treated as the Pattern Value Arguments.
    /// Empty string argument is equivalent to passing the search pattern '%'.
    Catalog::PrimaryKeys findPrimaryKeys(
        const StringType& table,
        const StringType& schema = StringType(),
        const StringType& Catalog = StringType());

    /// \brief Returns names of all catalogs (or databases) available in connected data source.
    ///
    /// Executes `SQLTable` function with `SQL_ALL_CATALOG` as Catalog search pattern.
    std::list<StringType> listCatalogs();

    /// \brief Returns names of all schemas available in connected data source.
    ///
    /// Executes `SQLTable` function with `SQL_ALL_SCHEMAS` as schema search pattern.
    std::list<StringType> listSchemas();

private:
    Connection conn_;
};

/// @}

// clang-format off
// 8888888888                            8888888888                         888    d8b
// 888                                   888                                888    Y8P
// 888                                   888                                888
// 8888888 888d888 .d88b.   .d88b.       8888888 888  888 88888b.   .d8888b 888888 888  .d88b.  88888b.  .d8888b
// 888     888P"  d8P  Y8b d8P  Y8b      888     888  888 888 "88b d88P"    888    888 d88""88b 888 "88b 88K
// 888     888    88888888 88888888      888     888  888 888  888 888      888    888 888  888 888  888 "Y8888b.
// 888     888    Y8b.     Y8b.          888     Y88b 888 888  888 Y88b.    Y88b.  888 Y88..88P 888  888      X88
// 888     888     "Y8888   "Y8888       888      "Y88888 888  888  "Y8888P  "Y888 888  "Y88P"  888  888  88888P'
// MARK: Free Functions -
// clang-format on

/// \addtogroup mainf Free Functions
/// \brief Convenience functions.
///
/// @{

/// \brief Information on a configured ODBC Driver.
struct Driver
{
    /// \brief Driver attributes.
    struct Attribute
    {
        Nanodbc::StringType keyword; ///< Driver keyword Attribute.
        Nanodbc::StringType value;   ///< Driver Attribute value.
    };

    Nanodbc::StringType name;       ///< Driver name.
    std::list<Attribute> attributes; ///< List of Driver attributes.
};

/// \brief Returns a list of ODBC drivers on your system.
std::list<Driver> listDrivers();

/// \brief Immediately opens, prepares, and executes the given query directly on the given
/// Connection.
/// \param conn The Connection where the Statement will be executed.
/// \param query The SQL query that will be executed.
/// \param batchOperations Numbers of rows to fetch per rowset, or the number of batch parameters
/// to process.
/// \param timeout The number in seconds before query timeout. Default is 0 indicating no timeout.
/// \return A Result set object.
/// \attention You will want to use transactions if you are doing batch operations because it will
///            prevent auto commits from occurring after each individual operation is executed.
/// \see open(), prepare(), execute(), Result, Transaction
Result
execute(Connection& conn, const StringType& query, long batchOperations = 1, long timeout = 0);

/// \brief Opens, prepares, and executes query directly without creating Result object.
/// \param conn The Connection where the Statement will be executed.
/// \param query The SQL query that will be executed.
/// \param batchOperations Rows to fetch per rowset, or number of batch parameters to process.
/// \param timeout The number in seconds before query timeout. Default is 0 indicating no timeout.
/// \return A Result set object.
/// \attention You will want to use transactions if you are doing batch operations because it will
///            prevent auto commits from occurring after each individual operation is executed.
/// \see open(), prepare(), execute(), Result, Transaction
void justExecute(
    Connection& conn,
    const StringType& query,
    long batchOperations = 1,
    long timeout = 0);

/// \brief Execute the previously prepared query now.
/// \param stmt The prepared Statement that will be executed.
/// \param batchOperations Rows to fetch per rowset, or the number of batch parameters to process.
/// \throws DatabaseError
/// \return A Result set object.
/// \attention You will want to use transactions if you are doing batch operations because it will
///            prevent auto commits from occurring after each individual operation is executed.
/// \see open(), prepare(), execute(), Result
Result execute(Statement& stmt, long batchOperations = 1);

/// \brief Execute the previously prepared query now and without creating Result object.
/// \param stmt The prepared Statement that will be executed.
/// \param batchOperations Rows to fetch per rowset, or the number of batch parameters to process.
/// \throws DatabaseError
/// \return A Result set object.
/// \attention You will want to use transactions if you are doing batch operations because it will
///            prevent auto commits from occurring after each individual operation is executed.
/// \see open(), prepare(), execute(), Result
void justExecute(Statement& stmt, long batchOperations = 1);

/// \brief Execute the previously prepared query now.
///
/// Executes within the context of a Transaction object, commits directly after execution.
/// \param stmt The prepared Statement that will be executed in batch.
/// \param batchOperations Rows to fetch per rowset, or the number of batch parameters to process.
/// \throws DatabaseError
/// \return A Result set object.
/// \see open(), prepare(), execute(), Result, Transaction
Result transact(Statement& stmt, long batchOperations);

/// \brief Execute the previously prepared query now and without creating Result object.
///
/// Executes within the context of a Transaction object, commits directly after execution.
/// \param stmt The prepared Statement that will be executed in batch.
/// \param batchOperations Rows to fetch per rowset, or the number of batch parameters to process.
/// \throws DatabaseError
/// \return A Result set object.
/// \see open(), prepare(), execute(), Result, Transaction
void justTransact(Statement& stmt, long batchOperations);

/// \brief Prepares the given Statement to execute on it associated Connection.
///
/// If the Statement is not open throws ProgrammingError.
/// \param stmt The prepared Statement that will be executed in batch.
/// \param query The SQL query that will be executed.
/// \param timeout The number in seconds before query timeout. Default is 0 indicating no timeout.
/// \see open()
/// \throws DatabaseError, ProgrammingError
void prepare(Statement& stmt, const StringType& query, long timeout = 0);

/// @}

} // namespace Nanodbc

#endif
