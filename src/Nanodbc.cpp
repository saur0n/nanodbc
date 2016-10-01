/// \file Nanodbc.cpp Implementation details.
#ifndef DOXYGEN

// ASCII art banners are helpful for code editors with a minimap display.
// Generated with http://patorjk.com/software/taag/#p=display&v=0&f=Colossal

#include "Nanodbc.h"

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>

#ifndef __clang__
#include <cstdint>
#endif

// User may redefine NANODBC_ASSERT macro in Nanodbc.h
#ifndef NANODBC_ASSERT
#include <cassert>
#define NANODBC_ASSERT(expr) assert(expr)
#endif

#ifdef NANODBC_USE_BOOST_CONVERT
#include <boost/locale/encoding_utf.hpp>
#else
#include <codecvt>
#endif

#if defined(_MSC_VER) && _MSC_VER <= 1800
// silence spurious Visual C++ warnings
#pragma warning(disable : 4244) // warning about integer conversion issues.
#pragma warning(disable : 4312) // warning about 64-bit portability issues.
#pragma warning(disable : 4996) // warning about snprintf() deprecated.
#endif

#ifdef __APPLE__
// silence spurious OS X deprecation warnings
#define MAC_OS_X_VERSION_MIN_REQUIRED MAC_OS_X_VERSION_10_6
#endif

#ifdef _WIN32
// needs to be included above sql.h for windows
#ifndef __MINGW32__
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>

// Large CLR User-Defined Types (ODBC)
// https://msdn.microsoft.com/en-us/library/bb677316.aspx
// Essentially, UDT is a varbinary type with additional metadata.
// Memory layout: SQLCHAR *(unsigned char *)
// C data type:   SQL_C_BINARY
// Value:         SQL_BINARY (-2)
#ifndef SQL_SS_UDT
#define SQL_SS_UDT (-151) // from sqlncli.h
#endif

// Default to ODBC version defined by NANODBC_ODBC_VERSION if provided.
#ifndef NANODBC_ODBC_VERSION
#ifdef SQL_OV_ODBC3_80
// Otherwise, use ODBC v3.8 if it's available...
#define NANODBC_ODBC_VERSION SQL_OV_ODBC3_80
#else
// or fallback to ODBC v3.x.
#define NANODBC_ODBC_VERSION SQL_OV_ODBC3
#endif
#endif

// clang-format off
// 888     888          d8b                       888
// 888     888          Y8P                       888
// 888     888                                    888
// 888     888 88888b.  888  .d8888b .d88b.   .d88888  .d88b.
// 888     888 888 "88b 888 d88P"   d88""88b d88" 888 d8P  Y8b
// 888     888 888  888 888 888     888  888 888  888 88888888
// Y88b. .d88P 888  888 888 Y88b.   Y88..88P Y88b 888 Y8b.
//  "Y88888P"  888  888 888  "Y8888P "Y88P"   "Y88888  "Y8888
// MARK: Unicode -
// clang-format on

#ifdef NANODBC_USE_UNICODE
#define NANODBC_FUNC(f) f##W
#define NANODBC_SQLCHAR SQLWCHAR
#else
#define NANODBC_FUNC(f) f
#define NANODBC_SQLCHAR SQLCHAR
#endif

#ifdef NANODBC_USE_IODBC_WIDE_STRINGS
typedef std::u32string wide_string_type;
#define NANODBC_CODECVT_TYPE std::codecvt_utf8
#else
#ifdef _MSC_VER
typedef std::wstring wide_string_type;
#define NANODBC_CODECVT_TYPE std::codecvt_utf8_utf16
#else
typedef std::u16string wide_string_type;
#define NANODBC_CODECVT_TYPE std::codecvt_utf8_utf16
#endif
#endif
typedef wide_string_type::value_type wide_char_t;

#if defined(_MSC_VER)
#ifndef NANODBC_USE_UNICODE
// Disable unicode in sqlucode.h on Windows when NANODBC_USE_UNICODE
// is not defined. This is required because unicode is enabled by
// default on many Windows systems.
#define SQL_NOUNICODEMAP
#endif
#endif

// clang-format off
//  .d88888b.  8888888b.  888888b.    .d8888b.       888b     d888
// d88P" "Y88b 888  "Y88b 888  "88b  d88P  Y88b      8888b   d8888
// 888     888 888    888 888  .88P  888    888      88888b.d88888
// 888     888 888    888 8888888K.  888             888Y88888P888  8888b.   .d8888b 888d888 .d88b.  .d8888b
// 888     888 888    888 888  "Y88b 888             888 Y888P 888     "88b d88P"    888P"  d88""88b 88K
// 888     888 888    888 888    888 888    888      888  Y8P  888 .d888888 888      888    888  888 "Y8888b.
// Y88b. .d88P 888  .d88P 888   d88P Y88b  d88P      888   "   888 888  888 Y88b.    888    Y88..88P      X88
//  "Y88888P"  8888888P"  8888888P"   "Y8888P"       888       888 "Y888888  "Y8888P 888     "Y88P"   88888P'
// MARK: ODBC Macros -
// clang-format on

#define NANODBC_STRINGIZE_I(text) #text
#define NANODBC_STRINGIZE(text) NANODBC_STRINGIZE_I(text)

// By making all calls to ODBC functions through this macro, we can easily get
// runtime debugging information of which ODBC functions are being called,
// in what order, and with what parameters by defining NANODBC_ODBC_API_DEBUG.
#ifdef NANODBC_ODBC_API_DEBUG
#include <iostream>
#define NANODBC_CALL_RC(FUNC, RC, ...)                                                             \
    do                                                                                             \
    {                                                                                              \
        std::cerr << __FILE__                                                                      \
            ":" NANODBC_STRINGIZE(__LINE__) " " NANODBC_STRINGIZE(FUNC) "(" #__VA_ARGS__ ")"       \
                  << std::endl;                                                                    \
        RC = FUNC(__VA_ARGS__);                                                                    \
    } while (false) /**/
#define NANODBC_CALL(FUNC, ...)                                                                    \
    do                                                                                             \
    {                                                                                              \
        std::cerr << __FILE__                                                                      \
            ":" NANODBC_STRINGIZE(__LINE__) " " NANODBC_STRINGIZE(FUNC) "(" #__VA_ARGS__ ")"       \
                  << std::endl;                                                                    \
        FUNC(__VA_ARGS__);                                                                         \
    } while (false) /**/
#else
#define NANODBC_CALL_RC(FUNC, RC, ...) RC = FUNC(__VA_ARGS__)
#define NANODBC_CALL(FUNC, ...) FUNC(__VA_ARGS__)
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

namespace
{
#ifdef NANODBC_ODBC_API_DEBUG
inline std::string return_code(RETCODE rc)
{
    switch (rc)
    {
    case SQL_SUCCESS:
        return "SQL_SUCCESS";
    case SQL_SUCCESS_WITH_INFO:
        return "SQL_SUCCESS_WITH_INFO";
    case SQL_ERROR:
        return "SQL_ERROR";
    case SQL_INVALID_HANDLE:
        return "SQL_INVALID_HANDLE";
    case SQL_NO_DATA:
        return "SQL_NO_DATA";
    case SQL_NEED_DATA:
        return "SQL_NEED_DATA";
    case SQL_STILL_EXECUTING:
        return "SQL_STILL_EXECUTING";
    }
    NANODBC_ASSERT(0);
    return "unknown"; // should never make it here
}
#endif

// Easy way to check if a return code signifies success.
inline bool success(RETCODE rc)
{
#ifdef NANODBC_ODBC_API_DEBUG
    std::cerr << "<-- rc: " << return_code(rc) << " | " << std::endl;
#endif
    return rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;
}

// Returns the array size.
template <typename T, std::size_t N>
inline std::size_t arrlen(T (&)[N])
{
    return N;
}

// Operates like strlen() on a character array.
template <typename T, std::size_t N>
inline std::size_t strarrlen(T (&a)[N])
{
    const T* s = &a[0];
    std::size_t i = 0;
    while (*s++ && i < N)
        i++;
    return i;
}

inline void convert(const wide_string_type& in, std::string& out)
{
#ifdef NANODBC_USE_BOOST_CONVERT
    using boost::locale::conv::utf_to_utf;
    out = utf_to_utf<char>(in.c_str(), in.c_str() + in.size());
#else
// Workaround for confirmed bug in VS2015. See:
// https://connect.microsoft.com/VisualStudio/Feedback/Details/1403302
// https://social.msdn.microsoft.com/Forums/en-US/8f40dcd8-c67f-4eba-9134-a19b9178e481
#if defined(_MSC_VER) && (_MSC_VER == 1900)
    auto p = reinterpret_cast<unsigned short const*>(in.data());
    out = std::wstring_convert<NANODBC_CODECVT_TYPE<unsigned short>, unsigned short>().to_bytes(
        p, p + in.size());
#else
    out = std::wstring_convert<NANODBC_CODECVT_TYPE<wide_char_t>, wide_char_t>().to_bytes(in);
#endif
#endif
}

#ifdef NANODBC_USE_UNICODE
inline void convert(const std::string& in, wide_string_type& out)
{
#ifdef NANODBC_USE_BOOST_CONVERT
    using boost::locale::conv::utf_to_utf;
    out = utf_to_utf<wide_char_t>(in.c_str(), in.c_str() + in.size());
// Workaround for confirmed bug in VS2015. See:
// https://connect.microsoft.com/VisualStudio/Feedback/Details/1403302
// https://social.msdn.microsoft.com/Forums/en-US/8f40dcd8-c67f-4eba-9134-a19b9178e481
#elif defined(_MSC_VER) && (_MSC_VER == 1900)
    auto s =
        std::wstring_convert<NANODBC_CODECVT_TYPE<unsigned short>, unsigned short>().from_bytes(in);
    auto p = reinterpret_cast<wide_char_t const*>(s.data());
    out.assign(p, p + s.size());
#else
    out = std::wstring_convert<NANODBC_CODECVT_TYPE<wide_char_t>, wide_char_t>().from_bytes(in);
#endif
}

inline void convert(const wide_string_type& in, wide_string_type& out)
{
    out = in;
}
#else
inline void convert(const std::string& in, std::string& out)
{
    out = in;
}
#endif

// Attempts to get the most recent ODBC error as a string.
// Always returns std::string, even in unicode mode.
inline std::string
recent_error(SQLHANDLE handle, SQLSMALLINT handle_type, long& native, std::string& state)
{
    Nanodbc::StringType Result;
    std::string rvalue;
    std::vector<NANODBC_SQLCHAR> sql_message(SQL_MAX_MESSAGE_LENGTH);
    sql_message[0] = '\0';

    SQLINTEGER i = 1;
    SQLINTEGER nativeError;
    SQLSMALLINT total_bytes;
    NANODBC_SQLCHAR sqlState[6];
    RETCODE rc;

    do
    {
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetDiagRec),
            rc,
            handle_type,
            handle,
            (SQLSMALLINT)i,
            sqlState,
            &nativeError,
            0,
            0,
            &total_bytes);

        if (success(rc) && total_bytes > 0)
            sql_message.resize(total_bytes + 1);

        if (rc == SQL_NO_DATA)
            break;

        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetDiagRec),
            rc,
            handle_type,
            handle,
            (SQLSMALLINT)i,
            sqlState,
            &nativeError,
            sql_message.data(),
            (SQLSMALLINT)sql_message.size(),
            &total_bytes);

        if (!success(rc))
        {
            convert(Result, rvalue);
            return rvalue;
        }

        if (!Result.empty())
            Result += ' ';

        Result += Nanodbc::StringType(sql_message.begin(), sql_message.end());
        i++;

// NOTE: unixODBC using PostgreSQL and SQLite drivers crash if you call SQLGetDiagRec()
// more than once. So as a (terrible but the best possible) workaround just exit
// this loop early on non-Windows systems.
#ifndef _MSC_VER
        break;
#endif
    } while (rc != SQL_NO_DATA);

    convert(Result, rvalue);
    state = std::string(&sqlState[0], &sqlState[arrlen(sqlState) - 1]);
    native = nativeError;
    std::string status = state;
    status += ": ";
    status += rvalue;

    // some drivers insert \0 into error messages for unknown reasons
    using std::replace;
    replace(status.begin(), status.end(), '\0', ' ');

    return status;
}

} // namespace

namespace Nanodbc
{

TypeIncompatipleError::TypeIncompatipleError()
    : std::runtime_error("type incompatible")
{
}

const char* TypeIncompatipleError::what() const NANODBC_NOEXCEPT
{
    return std::runtime_error::what();
}

NullAccessError::NullAccessError()
    : std::runtime_error("null access")
{
}

const char* NullAccessError::what() const NANODBC_NOEXCEPT
{
    return std::runtime_error::what();
}

IndexRangeError::IndexRangeError()
    : std::runtime_error("index out of range")
{
}

const char* IndexRangeError::what() const NANODBC_NOEXCEPT
{
    return std::runtime_error::what();
}

ProgrammingError::ProgrammingError(const std::string& info)
    : std::runtime_error(info.c_str())
{
}

const char* ProgrammingError::what() const NANODBC_NOEXCEPT
{
    return std::runtime_error::what();
}

DatabaseError::DatabaseError(void* handle, short handle_type, const std::string& info)
    : std::runtime_error(info)
    , nativeError(0)
    , sqlState("00000")
{
    message = std::string(std::runtime_error::what()) +
              recent_error(handle, handle_type, nativeError, sqlState);
}

const char* DatabaseError::what() const NANODBC_NOEXCEPT
{
    return message.c_str();
}

const long DatabaseError::native() const NANODBC_NOEXCEPT
{
    return nativeError;
}

const std::string DatabaseError::state() const NANODBC_NOEXCEPT
{
    return sqlState;
}

} // namespace Nanodbc

// Throwing exceptions using NANODBC_THROW_DATABASE_ERROR enables file name
// and line numbers to be inserted into the error message. Useful for debugging.
#define NANODBC_THROW_DATABASE_ERROR(handle, handle_type)                                          \
    throw Nanodbc::DatabaseError(                                                                 \
        handle, handle_type, __FILE__ ":" NANODBC_STRINGIZE(__LINE__) ": ") /**/

// clang-format off
// 8888888b.           888             d8b 888
// 888  "Y88b          888             Y8P 888
// 888    888          888                 888
// 888    888  .d88b.  888888  8888b.  888 888 .d8888b
// 888    888 d8P  Y8b 888        "88b 888 888 88K
// 888    888 88888888 888    .d888888 888 888 "Y8888b.
// 888  .d88P Y8b.     Y88b.  888  888 888 888      X88
// 8888888P"   "Y8888   "Y888 "Y888888 888 888  88888P'
// MARK: Details -
// clang-format on

#if !defined(NANODBC_DISABLE_ASYNC) && defined(SQL_ATTR_ASYNC_STMT_EVENT) &&                       \
    defined(SQL_API_SQLCOMPLETEASYNC)
#define NANODBC_DO_ASYNC_IMPL
#endif

namespace
{

using namespace std; // if int64_t is in std namespace (in c++11)

// A utility for calculating the ctype from the given type T.
// I essentially create a lookup table based on the MSDN ODBC documentation.
// See http://msdn.microsoft.com/en-us/library/windows/desktop/ms714556(v=vs.85).aspx
template <class T>
struct sql_ctype
{
};

template <>
struct sql_ctype<Nanodbc::StringType::value_type>
{
#ifdef NANODBC_USE_UNICODE
    static const SQLSMALLINT value = SQL_C_WCHAR;
#else
    static const SQLSMALLINT value = SQL_C_CHAR;
#endif
};

template <>
struct sql_ctype<short>
{
    static const SQLSMALLINT value = SQL_C_SSHORT;
};

template <>
struct sql_ctype<unsigned short>
{
    static const SQLSMALLINT value = SQL_C_USHORT;
};

template <>
struct sql_ctype<int32_t>
{
    static const SQLSMALLINT value = SQL_C_SLONG;
};

template <>
struct sql_ctype<uint32_t>
{
    static const SQLSMALLINT value = SQL_C_ULONG;
};

template <>
struct sql_ctype<int64_t>
{
    static const SQLSMALLINT value = SQL_C_SBIGINT;
};

template <>
struct sql_ctype<uint64_t>
{
    static const SQLSMALLINT value = SQL_C_UBIGINT;
};

template <>
struct sql_ctype<float>
{
    static const SQLSMALLINT value = SQL_C_FLOAT;
};

template <>
struct sql_ctype<double>
{
    static const SQLSMALLINT value = SQL_C_DOUBLE;
};

template <>
struct sql_ctype<Nanodbc::StringType>
{
#ifdef NANODBC_USE_UNICODE
    static const SQLSMALLINT value = SQL_C_WCHAR;
#else
    static const SQLSMALLINT value = SQL_C_CHAR;
#endif
};

template <>
struct sql_ctype<Nanodbc::date>
{
    static const SQLSMALLINT value = SQL_C_DATE;
};

template <>
struct sql_ctype<Nanodbc::time>
{
    static const SQLSMALLINT value = SQL_C_TIME;
};

template <>
struct sql_ctype<Nanodbc::timestamp>
{
    static const SQLSMALLINT value = SQL_C_TIMESTAMP;
};

// Encapsulates resources needed for column binding.
class bound_column
{
public:
    bound_column(const bound_column& rhs) = delete;
    bound_column& operator=(bound_column rhs) = delete;

    bound_column()
        : name_()
        , column_(0)
        , sqltype_(0)
        , sqlsize_(0)
        , scale_(0)
        , ctype_(0)
        , clen_(0)
        , blob_(false)
        , cbdata_(0)
        , pdata_(0)
    {
    }

    ~bound_column()
    {
        delete[] cbdata_;
        delete[] pdata_;
    }

public:
    Nanodbc::StringType name_;
    short column_;
    SQLSMALLINT sqltype_;
    SQLULEN sqlsize_;
    SQLSMALLINT scale_;
    SQLSMALLINT ctype_;
    SQLULEN clen_;
    bool blob_;
    Nanodbc::NullType* cbdata_;
    char* pdata_;
};

// Allocates the native ODBC handles.
inline void allocate_environment_handle(SQLHENV& env)
{
    RETCODE rc;
    NANODBC_CALL_RC(SQLAllocHandle, rc, SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(env, SQL_HANDLE_ENV);

    try
    {
        NANODBC_CALL_RC(
            SQLSetEnvAttr,
            rc,
            env,
            SQL_ATTR_ODBC_VERSION,
            (SQLPOINTER)NANODBC_ODBC_VERSION,
            SQL_IS_UINTEGER);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(env, SQL_HANDLE_ENV);
    }
    catch (...)
    {
        NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_ENV, env);
        throw;
    }
}

inline void allocate_handle(SQLHENV& env, SQLHDBC& conn)
{
    allocate_environment_handle(env);

    try
    {
        NANODBC_ASSERT(env);
        RETCODE rc;
        NANODBC_CALL_RC(SQLAllocHandle, rc, SQL_HANDLE_DBC, env, &conn);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(env, SQL_HANDLE_ENV);
    }
    catch (...)
    {
        NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_ENV, env);
        throw;
    }
}

} // namespace

// clang-format off
//  .d8888b.                                               888    d8b                             8888888                        888
// d88P  Y88b                                              888    Y8P                               888                          888
// 888    888                                              888                                      888                          888
// 888         .d88b.  88888b.  88888b.   .d88b.   .d8888b 888888 888  .d88b.  88888b.              888   88888b.d88b.  88888b.  888
// 888        d88""88b 888 "88b 888 "88b d8P  Y8b d88P"    888    888 d88""88b 888 "88b             888   888 "888 "88b 888 "88b 888
// 888    888 888  888 888  888 888  888 88888888 888      888    888 888  888 888  888             888   888  888  888 888  888 888
// Y88b  d88P Y88..88P 888  888 888  888 Y8b.     Y88b.    Y88b.  888 Y88..88P 888  888             888   888  888  888 888 d88P 888
//  "Y8888P"   "Y88P"  888  888 888  888  "Y8888   "Y8888P  "Y888 888  "Y88P"  888  888           8888888 888  888  888 88888P"  888
//                                                                                                                      888
//                                                                                                                      888
//                                                                                                                      888
// MARK: Connection Impl -
// clang-format on

namespace Nanodbc
{

class Connection::ConnectionImpl
{
public:
    ConnectionImpl(const ConnectionImpl&) = delete;
    ConnectionImpl& operator=(const ConnectionImpl&) = delete;

    ConnectionImpl()
        : env_(0)
        , conn_(0)
        , connected_(false)
        , transactions_(0)
        , rollback_(false)
    {
        allocate_handle(env_, conn_);
    }

    ConnectionImpl(
        const StringType& dsn,
        const StringType& user,
        const StringType& pass,
        long timeout)
        : env_(0)
        , conn_(0)
        , connected_(false)
        , transactions_(0)
        , rollback_(false)
    {
        allocate_handle(env_, conn_);
        try
        {
            connect(dsn, user, pass, timeout);
        }
        catch (...)
        {
            NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_DBC, conn_);
            NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_ENV, env_);
            throw;
        }
    }

    ConnectionImpl(const StringType& connectionString, long timeout)
        : env_(0)
        , conn_(0)
        , connected_(false)
        , transactions_(0)
        , rollback_(false)
    {
        allocate_handle(env_, conn_);
        try
        {
            connect(connectionString, timeout);
        }
        catch (...)
        {
            NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_DBC, conn_);
            NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_ENV, env_);
            throw;
        }
    }

    ~ConnectionImpl() NANODBC_NOEXCEPT
    {
        try
        {
            disconnect();
        }
        catch (...)
        {
            // ignore exceptions thrown during disconnect
        }
        NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_DBC, conn_);
        NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_ENV, env_);
    }

#if !defined(NANODBC_DISABLE_ASYNC) && defined(SQL_ATTR_ASYNC_DBC_EVENT)
    void enableAsync(void* eventHandle)
    {
        RETCODE rc;
        NANODBC_CALL_RC(
            SQLSetConnectAttr,
            rc,
            conn_,
            SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE,
            (SQLPOINTER)SQL_ASYNC_DBC_ENABLE_ON,
            SQL_IS_INTEGER);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

        NANODBC_CALL_RC(
            SQLSetConnectAttr, rc, conn_, SQL_ATTR_ASYNC_DBC_EVENT, eventHandle, SQL_IS_POINTER);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
    }

    void asyncComplete()
    {
        RETCODE rc, arc;
        NANODBC_CALL_RC(SQLCompleteAsync, rc, SQL_HANDLE_DBC, conn_, &arc);
        if (!success(rc) || !success(arc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

        connected_ = true;

        NANODBC_CALL_RC(
            SQLSetConnectAttr,
            rc,
            conn_,
            SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE,
            (SQLPOINTER)SQL_ASYNC_DBC_ENABLE_OFF,
            SQL_IS_INTEGER);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
    }
#endif // !NANODBC_DISABLE_ASYNC && SQL_ATTR_ASYNC_DBC_EVENT

    RETCODE connect(
        const StringType& dsn,
        const StringType& user,
        const StringType& pass,
        long timeout,
        void* eventHandle = nullptr)
    {
        disconnect();

        RETCODE rc;
        NANODBC_CALL_RC(SQLFreeHandle, rc, SQL_HANDLE_DBC, conn_);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

        NANODBC_CALL_RC(SQLAllocHandle, rc, SQL_HANDLE_DBC, env_, &conn_);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(env_, SQL_HANDLE_ENV);

        NANODBC_CALL_RC(
            SQLSetConnectAttr, rc, conn_, SQL_LOGIN_TIMEOUT, (SQLPOINTER)(std::intptr_t)timeout, 0);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

#if !defined(NANODBC_DISABLE_ASYNC) && defined(SQL_ATTR_ASYNC_DBC_EVENT)
        if (eventHandle != nullptr)
            enableAsync(eventHandle);
#endif

        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLConnect),
            rc,
            conn_,
            (NANODBC_SQLCHAR*)dsn.c_str(),
            SQL_NTS,
            !user.empty() ? (NANODBC_SQLCHAR*)user.c_str() : 0,
            SQL_NTS,
            !pass.empty() ? (NANODBC_SQLCHAR*)pass.c_str() : 0,
            SQL_NTS);
        if (!success(rc) && (eventHandle == nullptr || rc != SQL_STILL_EXECUTING))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

        connected_ = success(rc);

        return rc;
    }

    RETCODE
    connect(const StringType& connectionString, long timeout, void* eventHandle = nullptr)
    {
        disconnect();

        RETCODE rc;
        NANODBC_CALL_RC(SQLFreeHandle, rc, SQL_HANDLE_DBC, conn_);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

        NANODBC_CALL_RC(SQLAllocHandle, rc, SQL_HANDLE_DBC, env_, &conn_);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(env_, SQL_HANDLE_ENV);

        NANODBC_CALL_RC(
            SQLSetConnectAttr, rc, conn_, SQL_LOGIN_TIMEOUT, (SQLPOINTER)(std::intptr_t)timeout, 0);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

#if !defined(NANODBC_DISABLE_ASYNC) && defined(SQL_ATTR_ASYNC_DBC_EVENT)
        if (eventHandle != nullptr)
            enableAsync(eventHandle);
#endif

        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLDriverConnect),
            rc,
            conn_,
            0,
            (NANODBC_SQLCHAR*)connectionString.c_str(),
            SQL_NTS,
            nullptr,
            0,
            nullptr,
            SQL_DRIVER_NOPROMPT);
        if (!success(rc) && (eventHandle == nullptr || rc != SQL_STILL_EXECUTING))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);

        connected_ = success(rc);

        return rc;
    }

    bool connected() const { return connected_; }

    void disconnect()
    {
        if (connected())
        {
            RETCODE rc;
            NANODBC_CALL_RC(SQLDisconnect, rc, conn_);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
        }
        connected_ = false;
    }

    std::size_t transactions() const { return transactions_; }

    void* nativeDbcHandle() const { return conn_; }

    void* nativeEnvHandle() const { return env_; }

    StringType dbmsName() const
    {
        NANODBC_SQLCHAR name[255] = {0};
        SQLSMALLINT length(0);
        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetInfo),
            rc,
            conn_,
            SQL_DBMS_NAME,
            name,
            sizeof(name) / sizeof(NANODBC_SQLCHAR),
            &length);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
        return StringType(&name[0], &name[strarrlen(name)]);
    }

    StringType dbmsVersion() const
    {
        NANODBC_SQLCHAR version[255] = {0};
        SQLSMALLINT length(0);
        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetInfo),
            rc,
            conn_,
            SQL_DBMS_VER,
            version,
            sizeof(version) / sizeof(NANODBC_SQLCHAR),
            &length);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
        return StringType(&version[0], &version[strarrlen(version)]);
    }

    StringType driverName() const
    {
        NANODBC_SQLCHAR name[1024];
        SQLSMALLINT length;
        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetInfo),
            rc,
            conn_,
            SQL_DRIVER_NAME,
            name,
            sizeof(name) / sizeof(NANODBC_SQLCHAR),
            &length);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
        return StringType(&name[0], &name[strarrlen(name)]);
    }

    StringType databaseName() const
    {
        // FIXME: Allocate buffer of dynamic size as drivers do not agree on universal size
        // MySQL Driver limits MAX_NAME_LEN=255
        // PostgreSQL Driver MAX_INFO_STIRNG=128
        // MFC CDatabase allocates buffer dynamically.
        NANODBC_SQLCHAR name[255] = {0};
        SQLSMALLINT length(0);
        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetInfo),
            rc,
            conn_,
            SQL_DATABASE_NAME,
            name,
            sizeof(name) / sizeof(NANODBC_SQLCHAR),
            &length);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
        return StringType(&name[0], &name[strarrlen(name)]);
    }

    StringType catalogName() const
    {
        NANODBC_SQLCHAR name[SQL_MAX_OPTION_STRING_LENGTH] = {0};
        SQLINTEGER length(0);
        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLGetConnectAttr),
            rc,
            conn_,
            SQL_ATTR_CURRENT_CATALOG,
            name,
            sizeof(name) / sizeof(NANODBC_SQLCHAR),
            &length);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(conn_, SQL_HANDLE_DBC);
        return StringType(&name[0], &name[strarrlen(name)]);
    }

    std::size_t refTransaction() { return ++transactions_; }

    std::size_t unrefTransaction()
    {
        if (transactions_ > 0)
            --transactions_;
        return transactions_;
    }

    bool rollback() const { return rollback_; }

    void rollback(bool onoff) { rollback_ = onoff; }

private:
    HENV env_;
    HDBC conn_;
    bool connected_;
    std::size_t transactions_;
    bool rollback_; // if true, this Connection is marked for eventual Transaction rollback
};

} // namespace Nanodbc

// clang-format off
// 88888888888                                                  888    d8b                             8888888                        888
//     888                                                      888    Y8P                               888                          888
//     888                                                      888                                      888                          888
//     888  888d888 8888b.  88888b.  .d8888b   8888b.   .d8888b 888888 888  .d88b.  88888b.              888   88888b.d88b.  88888b.  888
//     888  888P"      "88b 888 "88b 88K          "88b d88P"    888    888 d88""88b 888 "88b             888   888 "888 "88b 888 "88b 888
//     888  888    .d888888 888  888 "Y8888b. .d888888 888      888    888 888  888 888  888             888   888  888  888 888  888 888
//     888  888    888  888 888  888      X88 888  888 Y88b.    Y88b.  888 Y88..88P 888  888             888   888  888  888 888 d88P 888
//     888  888    "Y888888 888  888  88888P' "Y888888  "Y8888P  "Y888 888  "Y88P"  888  888           8888888 888  888  888 88888P"  888
//                                                                                                                           888
//                                                                                                                           888
//                                                                                                                           888
// MARK: Transaction Impl -
// clang-format on

namespace Nanodbc
{

class Transaction::TransactionImpl
{
public:
    TransactionImpl(const TransactionImpl&) = delete;
    TransactionImpl& operator=(const TransactionImpl&) = delete;

    TransactionImpl(const class Connection& conn)
        : conn_(conn)
        , committed_(false)
    {
        if (conn_.transactions() == 0 && conn_.connected())
        {
            RETCODE rc;
            NANODBC_CALL_RC(
                SQLSetConnectAttr,
                rc,
                conn_.nativeDbcHandle(),
                SQL_ATTR_AUTOCOMMIT,
                (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
                SQL_IS_UINTEGER);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(conn_.nativeDbcHandle(), SQL_HANDLE_DBC);
        }
        conn_.refTransaction();
    }

    ~TransactionImpl() NANODBC_NOEXCEPT
    {
        if (!committed_)
        {
            conn_.rollback(true);
            conn_.unrefTransaction();
        }

        if (conn_.transactions() == 0 && conn_.connected())
        {
            if (conn_.rollback())
            {
                NANODBC_CALL(SQLEndTran, SQL_HANDLE_DBC, conn_.nativeDbcHandle(), SQL_ROLLBACK);
                conn_.rollback(false);
            }

            NANODBC_CALL(
                SQLSetConnectAttr,
                conn_.nativeDbcHandle(),
                SQL_ATTR_AUTOCOMMIT,
                (SQLPOINTER)SQL_AUTOCOMMIT_ON,
                SQL_IS_UINTEGER);
        }
    }

    void commit()
    {
        if (committed_)
            return;
        committed_ = true;
        if (conn_.unrefTransaction() == 0 && conn_.connected())
        {
            RETCODE rc;
            NANODBC_CALL_RC(SQLEndTran, rc, SQL_HANDLE_DBC, conn_.nativeDbcHandle(), SQL_COMMIT);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(conn_.nativeDbcHandle(), SQL_HANDLE_DBC);
        }
    }

    void rollback() NANODBC_NOEXCEPT
    {
        if (committed_)
            return;
        conn_.rollback(true);
    }

    class Connection& Connection() { return conn_; }

    const class Connection& Connection() const { return conn_; }

private:
    class Connection conn_;
    bool committed_;
};

} // namespace Nanodbc

// clang-format off
//  .d8888b.  888             888                                            888              8888888                        888
// d88P  Y88b 888             888                                            888                888                          888
// Y88b.      888             888                                            888                888                          888
//  "Y888b.   888888  8888b.  888888 .d88b.  88888b.d88b.   .d88b.  88888b.  888888             888   88888b.d88b.  88888b.  888
//     "Y88b. 888        "88b 888   d8P  Y8b 888 "888 "88b d8P  Y8b 888 "88b 888                888   888 "888 "88b 888 "88b 888
//       "888 888    .d888888 888   88888888 888  888  888 88888888 888  888 888                888   888  888  888 888  888 888
// Y88b  d88P Y88b.  888  888 Y88b. Y8b.     888  888  888 Y8b.     888  888 Y88b.              888   888  888  888 888 d88P 888
//  "Y8888P"   "Y888 "Y888888  "Y888 "Y8888  888  888  888  "Y8888  888  888  "Y888           8888888 888  888  888 88888P"  888
//                                                                                                                  888
//                                                                                                                  888
//                                                                                                                  888
// MARK: Statement Impl -
// clang-format on

namespace Nanodbc
{

class Statement::StatementImpl
{
public:
    StatementImpl(const StatementImpl&) = delete;
    StatementImpl& operator=(const StatementImpl&) = delete;

    StatementImpl()
        : stmt_(0)
        , open_(false)
        , conn_()
        , bind_len_or_null_()
#if defined(NANODBC_DO_ASYNC_IMPL)
        , async_(false)
        , async_enabled_(false)
        , async_event_(nullptr)
#endif
    {
    }

    StatementImpl(class Connection& conn)
        : stmt_(0)
        , open_(false)
        , conn_()
        , bind_len_or_null_()
#if defined(NANODBC_DO_ASYNC_IMPL)
        , async_(false)
        , async_enabled_(false)
        , async_event_(nullptr)
#endif
    {
        open(conn);
    }

    StatementImpl(class Connection& conn, const StringType& query, long timeout)
        : stmt_(0)
        , open_(false)
        , conn_()
        , bind_len_or_null_()
#if defined(NANODBC_DO_ASYNC_IMPL)
        , async_(false)
        , async_enabled_(false)
        , async_event_(nullptr)
#endif
    {
        prepare(conn, query, timeout);
    }

    ~StatementImpl() NANODBC_NOEXCEPT
    {
        if (open() && connected())
        {
            NANODBC_CALL(SQLCancel, stmt_);
            resetParameters();
            NANODBC_CALL(SQLFreeHandle, SQL_HANDLE_STMT, stmt_);
        }
    }

    void open(class Connection& conn)
    {
        close();
        RETCODE rc;
        NANODBC_CALL_RC(SQLAllocHandle, rc, SQL_HANDLE_STMT, conn.nativeDbcHandle(), &stmt_);
        open_ = success(rc);
        if (!open_)
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        conn_ = conn;
    }

    bool open() const { return open_; }

    bool connected() const { return conn_.connected(); }

    const class Connection& Connection() const { return conn_; }

    class Connection& Connection() { return conn_; }

    void* nativeStatementHandle() const { return stmt_; }

    void close()
    {
        if (open() && connected())
        {
            RETCODE rc;
            NANODBC_CALL_RC(SQLCancel, rc, stmt_);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

            resetParameters();

            NANODBC_CALL_RC(SQLFreeHandle, rc, SQL_HANDLE_STMT, stmt_);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        }

        open_ = false;
        stmt_ = 0;
    }

    void cancel()
    {
        RETCODE rc;
        NANODBC_CALL_RC(SQLCancel, rc, stmt_);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
    }

    void prepare(class Connection& conn, const StringType& query, long timeout)
    {
        open(conn);
        prepare(query, timeout);
    }

    RETCODE prepare(const StringType& query, long timeout, void* eventHandle = nullptr)
    {
        if (!open())
            throw ProgrammingError("Statement has no associated open Connection");

#if defined(NANODBC_DO_ASYNC_IMPL)
        if (eventHandle == nullptr)
            disableAsync();
        else
            enableAsync(eventHandle);
#endif

        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLPrepare),
            rc,
            stmt_,
            (NANODBC_SQLCHAR*)query.c_str(),
            (SQLINTEGER)query.size());
        if (!success(rc) && rc != SQL_STILL_EXECUTING)
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        this->timeout(timeout);

        return rc;
    }

    void timeout(long timeout)
    {
        RETCODE rc;
        NANODBC_CALL_RC(
            SQLSetStmtAttr,
            rc,
            stmt_,
            SQL_ATTR_QUERY_TIMEOUT,
            (SQLPOINTER)(std::intptr_t)timeout,
            0);

        // some drivers don't support timeout for statements,
        // so only raise the error if a non-default timeout was requested.
        if (!success(rc) && (timeout != 0))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
    }

#if defined(NANODBC_DO_ASYNC_IMPL)
    void enableAsync(void* eventHandle)
    {
        RETCODE rc;
        if (!async_enabled_)
        {
            NANODBC_CALL_RC(
                SQLSetStmtAttr,
                rc,
                stmt_,
                SQL_ATTR_ASYNC_ENABLE,
                (SQLPOINTER)SQL_ASYNC_ENABLE_ON,
                SQL_IS_INTEGER);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
            async_enabled_ = true;
        }

        if (async_event_ != eventHandle)
        {
            NANODBC_CALL_RC(
                SQLSetStmtAttr, rc, stmt_, SQL_ATTR_ASYNC_STMT_EVENT, eventHandle, SQL_IS_POINTER);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
            async_event_ = eventHandle;
        }
    }

    void disableAsync() const
    {
        if (async_enabled_)
        {
            RETCODE rc;
            NANODBC_CALL_RC(
                SQLSetStmtAttr,
                rc,
                stmt_,
                SQL_ATTR_ASYNC_ENABLE,
                (SQLPOINTER)SQL_ASYNC_ENABLE_OFF,
                SQL_IS_INTEGER);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
            async_enabled_ = false;
        }
    }

    bool async_helper(RETCODE rc)
    {
        if (rc == SQL_STILL_EXECUTING)
        {
            async_ = true;
            return true;
        }
        else if (success(rc))
        {
            async_ = false;
            return false;
        }
        else
        {
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        }
    }

    bool asyncPrepare(const StringType& query, void* eventHandle, long timeout)
    {
        return async_helper(prepare(query, timeout, eventHandle));
    }

    bool asyncExecuteDirect(
        class Connection& conn,
        void* eventHandle,
        const StringType& query,
        long batchOperations,
        long timeout,
        Statement& Statement)
    {
        return async_helper(
            justExecuteDirect(conn, query, batchOperations, timeout, Statement, eventHandle));
    }

    bool
    async_execute(void* eventHandle, long batchOperations, long timeout, Statement& Statement)
    {
        return async_helper(justExecute(batchOperations, timeout, Statement, eventHandle));
    }

    void call_complete_async()
    {
        if (async_)
        {
            RETCODE rc, arc;
            NANODBC_CALL_RC(SQLCompleteAsync, rc, SQL_HANDLE_STMT, stmt_, &arc);
            if (!success(rc) || !success(arc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        }
    }

    Result complete_execute(long batchOperations, Statement& Statement)
    {
        call_complete_async();

        return Result(Statement, batchOperations);
    }

    void completePrepare() { call_complete_async(); }

#endif
    Result executeDirect(
        class Connection& conn,
        const StringType& query,
        long batchOperations,
        long timeout,
        Statement& Statement)
    {
#ifdef NANODBC_HANDLE_NODATA_BUG
        const RETCODE rc = justExecuteDirect(conn, query, batchOperations, timeout, Statement);
        if (rc == SQL_NO_DATA)
            return Result();
#else
        justExecuteDirect(conn, query, batchOperations, timeout, Statement);
#endif
        return Result(Statement, batchOperations);
    }

    RETCODE justExecuteDirect(
        class Connection& conn,
        const StringType& query,
        long batchOperations,
        long timeout,
        Statement&, // Statement
        void* eventHandle = nullptr)
    {
        open(conn);

#if defined(NANODBC_DO_ASYNC_IMPL)
        if (eventHandle == nullptr)
            disableAsync();
        else
            enableAsync(eventHandle);
#endif

        RETCODE rc;
        NANODBC_CALL_RC(
            SQLSetStmtAttr,
            rc,
            stmt_,
            SQL_ATTR_PARAMSET_SIZE,
            (SQLPOINTER)(std::intptr_t)batchOperations,
            0);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        this->timeout(timeout);

        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLExecDirect), rc, stmt_, (NANODBC_SQLCHAR*)query.c_str(), SQL_NTS);
        if (!success(rc) && rc != SQL_NO_DATA && rc != SQL_STILL_EXECUTING)
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        return rc;
    }

    Result execute(long batchOperations, long timeout, Statement& Statement)
    {
#ifdef NANODBC_HANDLE_NODATA_BUG
        const RETCODE rc = justExecute(batchOperations, timeout, Statement);
        if (rc == SQL_NO_DATA)
            return Result();
#else
        justExecute(batchOperations, timeout, Statement);
#endif
        return Result(Statement, batchOperations);
    }

    RETCODE justExecute(
        long batchOperations,
        long timeout,
        Statement& /*Statement*/,
        void* eventHandle = nullptr)
    {
        RETCODE rc;

        if (open())
        {
            // The ODBC cursor must be closed before subsequent executions, as described
            // here
            // http://msdn.microsoft.com/en-us/library/windows/desktop/ms713584%28v=vs.85%29.aspx
            //
            // However, we don't necessarily want to call SQLCloseCursor() because that
            // will cause an invalid cursor state in the case that no cursor is currently open.
            // A better solution is to use SQLFreeStmt() with the SQL_CLOSE option, which has
            // the same effect without the undesired limitations.
            NANODBC_CALL_RC(SQLFreeStmt, rc, stmt_, SQL_CLOSE);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        }

#if defined(NANODBC_DO_ASYNC_IMPL)
        if (eventHandle == nullptr)
            disableAsync();
        else
            enableAsync(eventHandle);
#endif

        NANODBC_CALL_RC(
            SQLSetStmtAttr,
            rc,
            stmt_,
            SQL_ATTR_PARAMSET_SIZE,
            (SQLPOINTER)(std::intptr_t)batchOperations,
            0);
        if (!success(rc) && rc != SQL_NO_DATA)
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        this->timeout(timeout);

        NANODBC_CALL_RC(SQLExecute, rc, stmt_);
        if (!success(rc) && rc != SQL_NO_DATA && rc != SQL_STILL_EXECUTING)
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        return rc;
    }

    Result procedure_columns(
        const StringType& Catalog,
        const StringType& schema,
        const StringType& procedure,
        const StringType& column,
        Statement& Statement)
    {
        if (!open())
            throw ProgrammingError("Statement has no associated open Connection");

#if defined(NANODBC_DO_ASYNC_IMPL)
        disableAsync();
#endif

        RETCODE rc;
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLProcedureColumns),
            rc,
            stmt_,
            (NANODBC_SQLCHAR*)(Catalog.empty() ? nullptr : Catalog.c_str()),
            (Catalog.empty() ? 0 : SQL_NTS),
            (NANODBC_SQLCHAR*)(schema.empty() ? nullptr : schema.c_str()),
            (schema.empty() ? 0 : SQL_NTS),
            (NANODBC_SQLCHAR*)procedure.c_str(),
            SQL_NTS,
            (NANODBC_SQLCHAR*)(column.empty() ? nullptr : column.c_str()),
            (column.empty() ? 0 : SQL_NTS));

        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        return Result(Statement, 1);
    }

    long affectedRows() const
    {
        SQLLEN rows;
        RETCODE rc;
        NANODBC_CALL_RC(SQLRowCount, rc, stmt_, &rows);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        NANODBC_ASSERT(rows <= static_cast<SQLLEN>(std::numeric_limits<long>::max()));
        return static_cast<long>(rows);
    }

    short columns() const
    {
        SQLSMALLINT cols;
        RETCODE rc;

#if defined(NANODBC_DO_ASYNC_IMPL)
        disableAsync();
#endif

        NANODBC_CALL_RC(SQLNumResultCols, rc, stmt_, &cols);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        return cols;
    }

    void resetParameters() NANODBC_NOEXCEPT { NANODBC_CALL(SQLFreeStmt, stmt_, SQL_RESET_PARAMS); }

    unsigned long parameterSize(short param) const
    {
        RETCODE rc;
        SQLSMALLINT dataType;
        SQLSMALLINT nullable;
        SQLULEN parameterSize;

#if defined(NANODBC_DO_ASYNC_IMPL)
        disableAsync();
#endif

        NANODBC_CALL_RC(
            SQLDescribeParam, rc, stmt_, param + 1, &dataType, &parameterSize, 0, &nullable);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
        NANODBC_ASSERT(
            parameterSize <= static_cast<SQLULEN>(std::numeric_limits<unsigned long>::max()));
        return static_cast<unsigned long>(parameterSize);
    }

    static SQLSMALLINT param_type_from_direction(ParamDirection direction)
    {
        switch (direction)
        {
        case PARAM_IN:
            return SQL_PARAM_INPUT;
            break;
        case PARAM_OUT:
            return SQL_PARAM_OUTPUT;
            break;
        case PARAM_INOUT:
            return SQL_PARAM_INPUT_OUTPUT;
            break;
        case PARAM_RETURN:
            return SQL_PARAM_OUTPUT;
            break;
        default:
            NANODBC_ASSERT(false);
            throw ProgrammingError("unrecognized ParamDirection value");
        }
    }

    // initializes bind_len_or_null_ and gets information for bind
    void prepare_bind(
        short param,
        std::size_t elements,
        ParamDirection direction,
        SQLSMALLINT& dataType,
        SQLSMALLINT& param_type,
        SQLULEN& parameterSize,
        SQLSMALLINT& scale)
    {
        RETCODE rc;
        SQLSMALLINT nullable;

#if defined(NANODBC_DO_ASYNC_IMPL)
        disableAsync();
#endif

        NANODBC_CALL_RC(
            SQLDescribeParam, rc, stmt_, param + 1, &dataType, &parameterSize, &scale, &nullable);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);

        param_type = param_type_from_direction(direction);

        if (!bind_len_or_null_.count(param))
            bind_len_or_null_[param] = std::vector<NullType>();
        std::vector<NullType>().swap(bind_len_or_null_[param]);

        // ODBC weirdness: this must be at least 8 elements in size
        const std::size_t indicator_size = elements > 8 ? elements : 8;

        bind_len_or_null_[param].reserve(indicator_size);
        bind_len_or_null_[param].assign(indicator_size, SQL_NULL_DATA);
    }

    // calls actual ODBC bind parameter function
    template <class T>
    void bind_parameter(
        short param,
        const T* data,
        std::size_t, // elements
        SQLSMALLINT dataType,
        SQLSMALLINT param_type,
        SQLULEN parameterSize,
        SQLSMALLINT scale)
    {
        RETCODE rc;
        NANODBC_CALL_RC(
            SQLBindParameter,
            rc,
            stmt_,               // handle
            param + 1,           // parameter number
            param_type,          // input or output type
            sql_ctype<T>::value, // value type
            dataType,           // parameter type
            parameterSize,      // column size ignored for many types, but needed for strings
            scale,               // decimal digits
            (SQLPOINTER)data,    // parameter value
            parameterSize,      // buffer length
            bind_len_or_null_[param].data());

        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
    }

    // handles a single value (possibly a single string value), or multiple non-string values
    template <class T>
    void bind(short param, const T* values, std::size_t elements, ParamDirection direction);

    // handles multiple string values
    void bindStrings(
        short param,
        const StringType::value_type* values,
        std::size_t, // length
        std::size_t elements,
        ParamDirection direction)
    {
        bind(param, values, elements, direction);
    }

    // handles multiple null values
    void bind_null(short param, std::size_t elements)
    {
        SQLSMALLINT dataType;
        SQLSMALLINT param_type;
        SQLULEN parameterSize;
        SQLSMALLINT scale;
        prepare_bind(param, elements, PARAM_IN, dataType, param_type, parameterSize, scale);

        RETCODE rc;
        NANODBC_CALL_RC(
            SQLBindParameter,
            rc,
            stmt_,
            param + 1,
            param_type,
            SQL_C_CHAR,
            dataType,
            parameterSize, // column size ignored for many types, but needed for strings
            0,
            (SQLPOINTER)0, // null value
            0,             // parameterSize
            bind_len_or_null_[param].data());
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
    }

    // comparator for null sentry values
    template <class T>
    bool equals(const T& lhs, const T& rhs)
    {
        return lhs == rhs;
    }

    // handles multiple non-string values with a null sentry
    template <class T>
    void bind(
        short param,
        const T* values,
        std::size_t elements,
        const bool* nulls,
        const T* nullSentry,
        ParamDirection direction);

    // handles multiple string values
    void bindStrings(
        short param,
        const StringType::value_type* values,
        std::size_t length,
        std::size_t elements,
        const bool* nulls,
        const StringType::value_type* nullSentry,
        ParamDirection direction);

private:
    HSTMT stmt_;
    bool open_;
    class Connection conn_;
    std::map<short, std::vector<NullType>> bind_len_or_null_;

#if defined(NANODBC_DO_ASYNC_IMPL)
    bool async_;                 // true if Statement is currently in SQL_STILL_EXECUTING mode
    mutable bool async_enabled_; // true if Statement currently has SQL_ATTR_ASYNC_ENABLE =
                                 // SQL_ASYNC_ENABLE_ON
    void* async_event_;          // currently active event handle for async notifications
#endif
};

// Supports code like: query.bind(0, std_string.c_str())
// In this case, we need to pass nullptr to the final parameter of SQLBindParameter().
template <>
void Statement::StatementImpl::bind_parameter<StringType::value_type>(
    short param,
    const StringType::value_type* data,
    std::size_t elements,
    SQLSMALLINT dataType,
    SQLSMALLINT param_type,
    SQLULEN parameterSize,
    SQLSMALLINT scale)
{
    RETCODE rc;
    NANODBC_CALL_RC(
        SQLBindParameter,
        rc,
        stmt_,                                     // handle
        param + 1,                                 // parameter number
        param_type,                                // input or output type
        sql_ctype<StringType::value_type>::value, // value type
        dataType,                                 // parameter type
        parameterSize,   // column size ignored for many types, but needed for strings
        scale,            // decimal digits
        (SQLPOINTER)data, // parameter value
        parameterSize,   // buffer length
        (elements <= 1 ? nullptr : bind_len_or_null_[param].data()));

    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt_, SQL_HANDLE_STMT);
}

template <class T>
void Statement::StatementImpl::bind(
    short param,
    const T* values,
    std::size_t elements,
    ParamDirection direction)
{
    SQLSMALLINT dataType;
    SQLSMALLINT param_type;
    SQLULEN parameterSize;
    SQLSMALLINT scale;
    prepare_bind(param, elements, direction, dataType, param_type, parameterSize, scale);

    for (std::size_t i = 0; i < elements; ++i)
        bind_len_or_null_[param][i] = parameterSize;

    bind_parameter(param, values, elements, dataType, param_type, parameterSize, scale);
}

template <class T>
void Statement::StatementImpl::bind(
    short param,
    const T* values,
    std::size_t elements,
    const bool* nulls,
    const T* nullSentry,
    ParamDirection direction)
{
    SQLSMALLINT dataType;
    SQLSMALLINT param_type;
    SQLULEN parameterSize;
    SQLSMALLINT scale;
    prepare_bind(param, elements, direction, dataType, param_type, parameterSize, scale);

    for (std::size_t i = 0; i < elements; ++i)
        if ((nullSentry && !equals(values[i], *nullSentry)) || (nulls && !nulls[i]) || !nulls)
            bind_len_or_null_[param][i] = parameterSize;

    bind_parameter(param, values, elements, dataType, param_type, parameterSize, scale);
}

void Statement::StatementImpl::bindStrings(
    short param,
    const StringType::value_type* values,
    std::size_t length,
    std::size_t elements,
    const bool* nulls,
    const StringType::value_type* nullSentry,
    ParamDirection direction)
{
    SQLSMALLINT dataType;
    SQLSMALLINT param_type;
    SQLULEN parameterSize;
    SQLSMALLINT scale;
    prepare_bind(param, elements, direction, dataType, param_type, parameterSize, scale);

    if (nullSentry)
    {
        for (std::size_t i = 0; i < elements; ++i)
        {
            const StringType s_lhs(values + i * length, values + (i + 1) * length);
            const StringType s_rhs(nullSentry);
#if NANODBC_USE_UNICODE
            std::string narrow_lhs;
            narrow_lhs.reserve(s_lhs.size());
            convert(s_lhs, narrow_lhs);
            std::string narrow_rhs;
            narrow_rhs.reserve(s_rhs.size());
            convert(s_rhs, narrow_lhs);
            if (std::strncmp(narrow_lhs.c_str(), narrow_rhs.c_str(), length))
                bind_len_or_null_[param][i] = parameterSize;
#else
            if (std::strncmp(s_lhs.c_str(), s_rhs.c_str(), length))
                bind_len_or_null_[param][i] = parameterSize;
#endif
        }
    }
    else if (nulls)
    {
        for (std::size_t i = 0; i < elements; ++i)
        {
            if (!nulls[i])
                bind_len_or_null_[param][i] = SQL_NTS; // null terminated
        }
    }

    bind_parameter(param, values, elements, dataType, param_type, parameterSize, scale);
}

template <>
bool Statement::StatementImpl::equals(const date& lhs, const date& rhs)
{
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day;
}

template <>
bool Statement::StatementImpl::equals(const time& lhs, const time& rhs)
{
    return lhs.hour == rhs.hour && lhs.min == rhs.min && lhs.sec == rhs.sec;
}

template <>
bool Statement::StatementImpl::equals(const timestamp& lhs, const timestamp& rhs)
{
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day &&
           lhs.hour == rhs.hour && lhs.min == rhs.min && lhs.sec == rhs.sec &&
           lhs.fract == rhs.fract;
}

} // namespace Nanodbc

// clang-format off
// 8888888b.                            888 888              8888888                        888
// 888   Y88b                           888 888                888                          888
// 888    888                           888 888                888                          888
// 888   d88P .d88b.  .d8888b  888  888 888 888888             888   88888b.d88b.  88888b.  888
// 8888888P" d8P  Y8b 88K      888  888 888 888                888   888 "888 "88b 888 "88b 888
// 888 T88b  88888888 "Y8888b. 888  888 888 888                888   888  888  888 888  888 888
// 888  T88b Y8b.          X88 Y88b 888 888 Y88b.              888   888  888  888 888 d88P 888
// 888   T88b "Y8888   88888P'  "Y88888 888  "Y888           8888888 888  888  888 88888P"  888
//                                                                                 888
//                                                                                 888
//                                                                                 888
// MARK: Result Impl -
// clang-format on

namespace Nanodbc
{

class Result::ResultImpl
{
public:
    ResultImpl(const ResultImpl&) = delete;
    ResultImpl& operator=(const ResultImpl&) = delete;

    ResultImpl(Statement stmt, long rowsetSize)
        : stmt_(stmt)
        , rowset_size_(rowsetSize)
        , row_count_(0)
        , bound_columns_(0)
        , bound_columns_size_(0)
        , rowset_position_(0)
        , bound_columns_by_name_()
        , at_end_(false)
#if defined(NANODBC_DO_ASYNC_IMPL)
        , async_(false)
#endif
    {
        RETCODE rc;
        NANODBC_CALL_RC(
            SQLSetStmtAttr,
            rc,
            stmt_.nativeStatementHandle(),
            SQL_ATTR_ROW_ARRAY_SIZE,
            (SQLPOINTER)(std::intptr_t)rowset_size_,
            0);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);

        NANODBC_CALL_RC(
            SQLSetStmtAttr,
            rc,
            stmt_.nativeStatementHandle(),
            SQL_ATTR_ROWS_FETCHED_PTR,
            &row_count_,
            0);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);

        auto_bind();
    }

    ~ResultImpl() NANODBC_NOEXCEPT { cleanup_bound_columns(); }

    void* nativeStatementHandle() const { return stmt_.nativeStatementHandle(); }

    long rowsetSize() const { return rowset_size_; }

    long affectedRows() const { return stmt_.affectedRows(); }

    long rows() const NANODBC_NOEXCEPT
    {
        NANODBC_ASSERT(row_count_ <= static_cast<SQLULEN>(std::numeric_limits<long>::max()));
        return static_cast<long>(row_count_);
    }

    short columns() const { return stmt_.columns(); }

    bool first()
    {
        rowset_position_ = 0;
        return fetch(0, SQL_FETCH_FIRST);
    }

    bool last()
    {
        rowset_position_ = 0;
        return fetch(0, SQL_FETCH_LAST);
    }

    bool next(void* eventHandle = nullptr)
    {
        if (rows() && ++rowset_position_ < rowset_size_)
            return rowset_position_ < rows();
        rowset_position_ = 0;
        return fetch(0, SQL_FETCH_NEXT, eventHandle);
    }

#if defined(NANODBC_DO_ASYNC_IMPL)
    bool asyncNext(void* eventHandle)
    {
        async_ = next(eventHandle);
        return async_;
    }

    bool completeNext()
    {
        if (async_)
        {
            RETCODE rc, arc;
            NANODBC_CALL_RC(
                SQLCompleteAsync, rc, SQL_HANDLE_STMT, stmt_.nativeStatementHandle(), &arc);
            if (arc == SQL_NO_DATA)
            {
                at_end_ = true;
                return false;
            }
            if (!success(rc) || !success(arc))
                NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
            async_ = false;
        }
        return !at_end_;
    }
#endif

    bool prior()
    {
        if (rows() && --rowset_position_ >= 0)
            return true;
        rowset_position_ = 0;
        return fetch(0, SQL_FETCH_PRIOR);
    }

    bool move(long row)
    {
        rowset_position_ = 0;
        return fetch(row, SQL_FETCH_ABSOLUTE);
    }

    bool skip(long rows)
    {
        rowset_position_ += rows;
        if (this->rows() && rowset_position_ < rowset_size_)
            return rowset_position_ < this->rows();
        rowset_position_ = 0;
        return fetch(rows, SQL_FETCH_RELATIVE);
    }

    unsigned long position() const
    {
        SQLULEN pos = 0; // necessary to initialize to 0
        RETCODE rc;
        NANODBC_CALL_RC(
            SQLGetStmtAttr,
            rc,
            stmt_.nativeStatementHandle(),
            SQL_ATTR_ROW_NUMBER,
            &pos,
            SQL_IS_UINTEGER,
            0);
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);

        // MSDN (https://msdn.microsoft.com/en-us/library/ms712631.aspx):
        // If the number of the current row cannot be determined or
        // there is no current row, the Driver returns 0.
        // Otherwise, valid row number is returned, starting at 1.
        //
        // NOTE: We try to address incorrect implementation in some drivers (e.g. SQLite ODBC)
        // which instead of 0 return SQL_ROW_NUMBER_UNKNOWN(-2) .
        if (pos == 0 || pos == static_cast<SQLULEN>(SQL_ROW_NUMBER_UNKNOWN))
            return 0;

        NANODBC_ASSERT(pos <= static_cast<SQLULEN>(std::numeric_limits<unsigned long>::max()));
        return static_cast<unsigned long>(pos) + rowset_position_;
    }

    bool atEnd() const NANODBC_NOEXCEPT
    {
        if (at_end_)
            return true;
        SQLULEN pos = 0; // necessary to initialize to 0
        RETCODE rc;
        NANODBC_CALL_RC(
            SQLGetStmtAttr,
            rc,
            stmt_.nativeStatementHandle(),
            SQL_ATTR_ROW_NUMBER,
            &pos,
            SQL_IS_UINTEGER,
            0);
        return (!success(rc) || rows() < 0 || pos - 1 > static_cast<unsigned long>(rows()));
    }

    bool isNull(short column) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        bound_column& col = bound_columns_[column];
        if (rowset_position_ >= rows())
            throw IndexRangeError();
        return col.cbdata_[rowset_position_] == SQL_NULL_DATA;
    }

    bool isNull(const StringType& columnName) const
    {
        const short column = this->column(columnName);
        return isNull(column);
    }

    short column(const StringType& columnName) const
    {
        typedef std::map<StringType, bound_column*>::const_iterator iter;
        iter i = bound_columns_by_name_.find(columnName);
        if (i == bound_columns_by_name_.end())
            throw IndexRangeError();
        return i->second->column_;
    }

    StringType columnName(short column) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        return bound_columns_[column].name_;
    }

    long columnSize(short column) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        bound_column& col = bound_columns_[column];
        NANODBC_ASSERT(col.sqlsize_ <= static_cast<SQLULEN>(std::numeric_limits<long>::max()));
        return static_cast<long>(col.sqlsize_);
    }

    int columnSize(const StringType& columnName) const
    {
        const short column = this->column(columnName);
        return columnSize(column);
    }

    int columnDecimalDigits(short column) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        bound_column& col = bound_columns_[column];
        return col.scale_;
    }

    int columnDecimalDigits(const StringType& columnName) const
    {
        const short column = this->column(columnName);
        bound_column& col = bound_columns_[column];
        return col.scale_;
    }

    int columnDatatype(short column) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        bound_column& col = bound_columns_[column];
        return col.sqltype_;
    }

    int columnDatatype(const StringType& columnName) const
    {
        const short column = this->column(columnName);
        bound_column& col = bound_columns_[column];
        return col.sqltype_;
    }

    int columnCDatatype(short column) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        bound_column& col = bound_columns_[column];
        return col.ctype_;
    }

    int columnCDatatype(const StringType& columnName) const
    {
        const short column = this->column(columnName);
        bound_column& col = bound_columns_[column];
        return col.ctype_;
    }

    bool nextResult()
    {
        RETCODE rc;

#if defined(NANODBC_DO_ASYNC_IMPL)
        stmt_.disableAsync();
#endif

        NANODBC_CALL_RC(SQLMoreResults, rc, stmt_.nativeStatementHandle());
        if (rc == SQL_NO_DATA)
            return false;
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
        auto_bind();
        return true;
    }

    template <class T>
    void getRef(short column, T& Result) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        if (isNull(column))
            throw NullAccessError();
        get_ref_impl<T>(column, Result);
    }

    template <class T>
    void getRef(short column, const T& fallback, T& Result) const
    {
        if (column >= bound_columns_size_)
            throw IndexRangeError();
        if (isNull(column))
        {
            Result = fallback;
            return;
        }
        get_ref_impl<T>(column, Result);
    }

    template <class T>
    void getRef(const StringType& columnName, T& Result) const
    {
        const short column = this->column(columnName);
        if (isNull(column))
            throw NullAccessError();
        get_ref_impl<T>(column, Result);
    }

    template <class T>
    void getRef(const StringType& columnName, const T& fallback, T& Result) const
    {
        const short column = this->column(columnName);
        if (isNull(column))
        {
            Result = fallback;
            return;
        }
        get_ref_impl<T>(column, Result);
    }

    template <class T>
    T get(short column) const
    {
        T Result;
        getRef(column, Result);
        return Result;
    }

    template <class T>
    T get(short column, const T& fallback) const
    {
        T Result;
        getRef(column, fallback, Result);
        return Result;
    }

    template <class T>
    T get(const StringType& columnName) const
    {
        T Result;
        getRef(columnName, Result);
        return Result;
    }

    template <class T>
    T get(const StringType& columnName, const T& fallback) const
    {
        T Result;
        getRef(columnName, fallback, Result);
        return Result;
    }

private:
    template <class T>
    void get_ref_impl(short column, T& Result) const;

    void before_move() NANODBC_NOEXCEPT
    {
        for (short i = 0; i < bound_columns_size_; ++i)
        {
            bound_column& col = bound_columns_[i];
            for (long j = 0; j < rowset_size_; ++j)
                col.cbdata_[j] = 0;
            if (col.blob_ && col.pdata_)
                release_bound_resources(i);
        }
    }

    void release_bound_resources(short column) NANODBC_NOEXCEPT
    {
        NANODBC_ASSERT(column < bound_columns_size_);
        bound_column& col = bound_columns_[column];
        delete[] col.pdata_;
        col.pdata_ = 0;
        col.clen_ = 0;
    }

    void cleanup_bound_columns() NANODBC_NOEXCEPT
    {
        before_move();
        delete[] bound_columns_;
        bound_columns_ = nullptr;
        bound_columns_size_ = 0;
        bound_columns_by_name_.clear();
    }

    // If eventHandle is specified, fetch returns true iff the Statement is still executing
    bool fetch(long rows, SQLUSMALLINT orientation, void* eventHandle = nullptr)
    {
        before_move();

#if defined(NANODBC_DO_ASYNC_IMPL)
        if (eventHandle == nullptr)
            stmt_.disableAsync();
        else
            stmt_.enableAsync(eventHandle);
#endif // !NANODBC_DISABLE_ASYNC && SQL_ATTR_ASYNC_STMT_EVENT && SQL_API_SQLCOMPLETEASYNC

        RETCODE rc;
        NANODBC_CALL_RC(SQLFetchScroll, rc, stmt_.nativeStatementHandle(), orientation, rows);
        if (rc == SQL_NO_DATA)
        {
            at_end_ = true;
            return false;
        }
#if defined(NANODBC_DO_ASYNC_IMPL)
        if (eventHandle != nullptr)
            return rc == SQL_STILL_EXECUTING;
#endif
        if (!success(rc))
            NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
        return true;
    }

    void auto_bind()
    {
        cleanup_bound_columns();

        const short n_columns = columns();
        if (n_columns < 1)
            return;

        NANODBC_ASSERT(!bound_columns_);
        NANODBC_ASSERT(!bound_columns_size_);
        bound_columns_ = new bound_column[n_columns];
        bound_columns_size_ = n_columns;

        RETCODE rc;
        NANODBC_SQLCHAR columnName[1024];
        SQLSMALLINT sqltype, scale, nullable, len;
        SQLULEN sqlsize;

#if defined(NANODBC_DO_ASYNC_IMPL)
        stmt_.disableAsync();
#endif

        for (SQLSMALLINT i = 0; i < n_columns; ++i)
        {
            NANODBC_CALL_RC(
                NANODBC_FUNC(SQLDescribeCol),
                rc,
                stmt_.nativeStatementHandle(),
                i + 1,
                (NANODBC_SQLCHAR*)columnName,
                sizeof(columnName) / sizeof(NANODBC_SQLCHAR),
                &len,
                &sqltype,
                &sqlsize,
                &scale,
                &nullable);
            if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);

            // Adjust the sqlsize parameter in case of "unlimited" data (varchar(max),
            // nvarchar(max)).
            bool is_blob = false;

            if (sqlsize == 0)
            {
                switch (sqltype)
                {
                case SQL_VARCHAR:
                case SQL_WVARCHAR:
                {
                    // Divide in half, due to sqlsize being 32-bit in Win32 (and 64-bit in x64)
                    // sqlsize = std::numeric_limits<int32_t>::max() / 2 - 1;
                    is_blob = true;
                }
                }
            }

            bound_column& col = bound_columns_[i];
            col.name_ = reinterpret_cast<StringType::value_type*>(columnName);
            col.column_ = i;
            col.sqltype_ = sqltype;
            col.sqlsize_ = sqlsize;
            col.scale_ = scale;
            bound_columns_by_name_[col.name_] = &col;

            using namespace std; // if int64_t is in std namespace (in c++11)
            switch (col.sqltype_)
            {
            case SQL_BIT:
            case SQL_TINYINT:
            case SQL_SMALLINT:
            case SQL_INTEGER:
            case SQL_BIGINT:
                col.ctype_ = SQL_C_SBIGINT;
                col.clen_ = sizeof(int64_t);
                break;
            case SQL_DOUBLE:
            case SQL_FLOAT:
            case SQL_DECIMAL:
            case SQL_REAL:
            case SQL_NUMERIC:
                col.ctype_ = SQL_C_DOUBLE;
                col.clen_ = sizeof(double);
                break;
            case SQL_DATE:
            case SQL_TYPE_DATE:
                col.ctype_ = SQL_C_DATE;
                col.clen_ = sizeof(date);
                break;
            case SQL_TIME:
            case SQL_TYPE_TIME:
                col.ctype_ = SQL_C_TIME;
                col.clen_ = sizeof(time);
                break;
            case SQL_TIMESTAMP:
            case SQL_TYPE_TIMESTAMP:
                col.ctype_ = SQL_C_TIMESTAMP;
                col.clen_ = sizeof(timestamp);
                break;
            case SQL_CHAR:
            case SQL_VARCHAR:
                col.ctype_ = SQL_C_CHAR;
                col.clen_ = (col.sqlsize_ + 1) * sizeof(SQLCHAR);
                if (is_blob)
                {
                    col.clen_ = 0;
                    col.blob_ = true;
                }
                break;
            case SQL_WCHAR:
            case SQL_WVARCHAR:
                col.ctype_ = SQL_C_WCHAR;
                col.clen_ = (col.sqlsize_ + 1) * sizeof(SQLWCHAR);
                if (is_blob)
                {
                    col.clen_ = 0;
                    col.blob_ = true;
                }
                break;
            case SQL_LONGVARCHAR:
                col.ctype_ = SQL_C_CHAR;
                col.blob_ = true;
                col.clen_ = 0;
                break;
            case SQL_BINARY:
            case SQL_VARBINARY:
            case SQL_LONGVARBINARY:
            case SQL_SS_UDT: // MSDN: Essentially, UDT is a varbinary type with additional metadata.
                col.ctype_ = SQL_C_BINARY;
                col.blob_ = true;
                col.clen_ = 0;
                break;
            default:
                col.ctype_ = sql_ctype<StringType>::value;
                col.clen_ = 128;
                break;
            }
        }

        for (SQLSMALLINT i = 0; i < n_columns; ++i)
        {
            bound_column& col = bound_columns_[i];
            col.cbdata_ = new NullType[rowset_size_];
            if (col.blob_)
            {
                NANODBC_CALL_RC(
                    SQLBindCol,
                    rc,
                    stmt_.nativeStatementHandle(),
                    i + 1,
                    col.ctype_,
                    0,
                    0,
                    col.cbdata_);
                if (!success(rc))
                    NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
            }
            else
            {
                col.pdata_ = new char[rowset_size_ * col.clen_];
                NANODBC_CALL_RC(
                    SQLBindCol,
                    rc,
                    stmt_.nativeStatementHandle(),
                    i + 1,        // ColumnNumber
                    col.ctype_,   // TargetType
                    col.pdata_,   // TargetValuePtr
                    col.clen_,    // BufferLength
                    col.cbdata_); // StrLen_or_Ind
                if (!success(rc))
                    NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
            }
        }
    }

private:
    Statement stmt_;
    const long rowset_size_;
    SQLULEN row_count_;
    bound_column* bound_columns_;
    short bound_columns_size_;
    long rowset_position_;
    std::map<StringType, bound_column*> bound_columns_by_name_;
    bool at_end_;
#if defined(NANODBC_DO_ASYNC_IMPL)
    bool async_; // true if Statement is currently in SQL_STILL_EXECUTING mode
#endif
};

template <>
inline void Result::ResultImpl::get_ref_impl<date>(short column, date& Result) const
{
    bound_column& col = bound_columns_[column];
    switch (col.ctype_)
    {
    case SQL_C_DATE:
        Result = *reinterpret_cast<date*>(col.pdata_ + rowset_position_ * col.clen_);
        return;
    case SQL_C_TIMESTAMP:
    {
        timestamp stamp = *reinterpret_cast<timestamp*>(col.pdata_ + rowset_position_ * col.clen_);
        date d = {stamp.year, stamp.month, stamp.day};
        Result = d;
        return;
    }
    }
    throw TypeIncompatipleError();
}

template <>
inline void Result::ResultImpl::get_ref_impl<time>(short column, time& Result) const
{
    bound_column& col = bound_columns_[column];
    switch (col.ctype_)
    {
    case SQL_C_TIME:
        Result = *reinterpret_cast<time*>(col.pdata_ + rowset_position_ * col.clen_);
        return;
    case SQL_C_TIMESTAMP:
    {
        timestamp stamp = *reinterpret_cast<timestamp*>(col.pdata_ + rowset_position_ * col.clen_);
        time t = {stamp.hour, stamp.min, stamp.sec};
        Result = t;
        return;
    }
    }
    throw TypeIncompatipleError();
}

template <>
inline void Result::ResultImpl::get_ref_impl<timestamp>(short column, timestamp& Result) const
{
    bound_column& col = bound_columns_[column];
    switch (col.ctype_)
    {
    case SQL_C_DATE:
    {
        date d = *reinterpret_cast<date*>(col.pdata_ + rowset_position_ * col.clen_);
        timestamp stamp = {d.year, d.month, d.day, 0, 0, 0, 0};
        Result = stamp;
        return;
    }
    case SQL_C_TIMESTAMP:
        Result = *reinterpret_cast<timestamp*>(col.pdata_ + rowset_position_ * col.clen_);
        return;
    }
    throw TypeIncompatipleError();
}

template <>
inline void Result::ResultImpl::get_ref_impl<StringType>(short column, StringType& Result) const
{
    bound_column& col = bound_columns_[column];
    const SQLULEN columnSize = col.sqlsize_;

    switch (col.ctype_)
    {
    case SQL_C_CHAR:
    case SQL_C_BINARY:
    {
        if (col.blob_)
        {
            // Input is always std::string, while output may be std::string or wide_string_type
            std::string out;
            // The length of the data available to return, decreasing with subsequent SQLGetData
            // calls.
            // But, NOT the length of data returned into the buffer (apart from the final call).
            SQLLEN ValueLenOrInd;
            SQLRETURN rc;

#if defined(NANODBC_DO_ASYNC_IMPL)
            stmt_.disableAsync();
#endif

            void* handle = nativeStatementHandle();
            do
            {
                char buffer[1024] = {0};
                const std::size_t buffer_size = sizeof(buffer);
                NANODBC_CALL_RC(
                    SQLGetData,
                    rc,
                    handle,          // StatementHandle
                    column + 1,      // Col_or_Param_Num
                    col.ctype_,      // TargetType
                    buffer,          // TargetValuePtr
                    buffer_size,     // BufferLength
                    &ValueLenOrInd); // StrLen_or_IndPtr
                if (ValueLenOrInd > 0)
                    out.append(
                        buffer,
                        std::min<std::size_t>(
                            ValueLenOrInd,
                            col.ctype_ == SQL_C_BINARY ? buffer_size : buffer_size - 1));
                else if (ValueLenOrInd == SQL_NULL_DATA)
                    col.cbdata_[rowset_position_] = (SQLINTEGER)SQL_NULL_DATA;
                // Sequence of successful calls is:
                // SQL_NO_DATA or SQL_SUCCESS_WITH_INFO followed by SQL_SUCCESS.
            } while (rc == SQL_SUCCESS_WITH_INFO);
            if (rc == SQL_SUCCESS || rc == SQL_NO_DATA)
                convert(out, Result);
            else if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
        }
        else
        {
            const char* s = col.pdata_ + rowset_position_ * col.clen_;
            const std::string::size_type str_size = std::strlen(s);
            Result.assign(s, s + str_size);
        }
        return;
    }

    case SQL_C_WCHAR:
    {
        if (col.blob_)
        {
            // Input is always wide_string_type, output might be std::string or wide_string_type.
            // Use a string builder to build the output string.
            wide_string_type out;
            // The length of the data available to return, decreasing with subsequent SQLGetData
            // calls.
            // But, NOT the length of data returned into the buffer (apart from the final call).
            SQLLEN ValueLenOrInd;
            SQLRETURN rc;

#if defined(NANODBC_DO_ASYNC_IMPL)
            stmt_.disableAsync();
#endif

            void* handle = nativeStatementHandle();
            do
            {
                wide_char_t buffer[512] = {0};
                const std::size_t buffer_size = sizeof(buffer);
                NANODBC_CALL_RC(
                    SQLGetData,
                    rc,
                    handle,          // StatementHandle
                    column + 1,      // Col_or_Param_Num
                    col.ctype_,      // TargetType
                    buffer,          // TargetValuePtr
                    buffer_size,     // BufferLength
                    &ValueLenOrInd); // StrLen_or_IndPtr
                if (ValueLenOrInd > 0)
                    out.append(
                        buffer,
                        std::min<std::size_t>(
                            ValueLenOrInd / sizeof(wide_char_t),
                            (buffer_size / sizeof(wide_char_t)) - 1));
                else if (ValueLenOrInd == SQL_NULL_DATA)
                    col.cbdata_[rowset_position_] = (SQLINTEGER)SQL_NULL_DATA;
                // Sequence of successful calls is:
                // SQL_NO_DATA or SQL_SUCCESS_WITH_INFO followed by SQL_SUCCESS.
            } while (rc == SQL_SUCCESS_WITH_INFO);
            if (rc == SQL_SUCCESS || rc == SQL_NO_DATA)
                convert(out, Result);
            else if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
            ;
        }
        else
        {
            // Type is unicode in the database, convert if necessary
            const SQLWCHAR* s =
                reinterpret_cast<SQLWCHAR*>(col.pdata_ + rowset_position_ * col.clen_);
            const StringType::size_type str_size =
                col.cbdata_[rowset_position_] / sizeof(SQLWCHAR);
            wide_string_type temp(s, s + str_size);
            convert(temp, Result);
        }
        return;
    }

    case SQL_C_GUID:
    {
        const char* s = col.pdata_ + rowset_position_ * col.clen_;
        Result.assign(s, s + columnSize);
        return;
    }

    case SQL_C_LONG:
    {
        std::string buffer;
        buffer.reserve(columnSize + 1); // ensure terminating null
        buffer.resize(buffer.capacity());
        using std::fill;
        fill(buffer.begin(), buffer.end(), '\0');
        const int32_t data = *reinterpret_cast<int32_t*>(col.pdata_ + rowset_position_ * col.clen_);
        const int bytes =
            std::snprintf(const_cast<char*>(buffer.data()), columnSize + 1, "%d", data);
        if (bytes == -1)
            throw TypeIncompatipleError();
        else if ((SQLULEN)bytes < columnSize)
            buffer.resize(bytes);
        buffer.resize(std::strlen(buffer.data())); // drop any trailing nulls
        Result.reserve(buffer.size() * sizeof(StringType::value_type));
        convert(buffer, Result);
        return;
    }

    case SQL_C_SBIGINT:
    {
        using namespace std; // in case intmax_t is in namespace std
        std::string buffer;
        buffer.reserve(columnSize + 1); // ensure terminating null
        buffer.resize(buffer.capacity());
        using std::fill;
        fill(buffer.begin(), buffer.end(), '\0');
        const intmax_t data =
            (intmax_t) * reinterpret_cast<int64_t*>(col.pdata_ + rowset_position_ * col.clen_);
        const int bytes =
            std::snprintf(const_cast<char*>(buffer.data()), columnSize + 1, "%jd", data);
        if (bytes == -1)
            throw TypeIncompatipleError();
        else if ((SQLULEN)bytes < columnSize)
            buffer.resize(bytes);
        buffer.resize(std::strlen(buffer.data())); // drop any trailing nulls
        Result.reserve(buffer.size() * sizeof(StringType::value_type));
        convert(buffer, Result);
        return;
    }

    case SQL_C_FLOAT:
    {
        std::string buffer;
        buffer.reserve(columnSize + 1); // ensure terminating null
        buffer.resize(buffer.capacity());
        using std::fill;
        fill(buffer.begin(), buffer.end(), '\0');
        const float data = *reinterpret_cast<float*>(col.pdata_ + rowset_position_ * col.clen_);
        const int bytes =
            std::snprintf(const_cast<char*>(buffer.data()), columnSize + 1, "%f", data);
        if (bytes == -1)
            throw TypeIncompatipleError();
        else if ((SQLULEN)bytes < columnSize)
            buffer.resize(bytes);
        buffer.resize(std::strlen(buffer.data())); // drop any trailing nulls
        Result.reserve(buffer.size() * sizeof(StringType::value_type));
        convert(buffer, Result);
        return;
    }

    case SQL_C_DOUBLE:
    {
        std::string buffer;
        const SQLULEN width = columnSize + 2; // account for decimal mark and sign
        buffer.reserve(width + 1);             // ensure terminating null
        buffer.resize(buffer.capacity());
        using std::fill;
        fill(buffer.begin(), buffer.end(), '\0');
        const double data = *reinterpret_cast<double*>(col.pdata_ + rowset_position_ * col.clen_);
        const int bytes = std::snprintf(
            const_cast<char*>(buffer.data()),
            width + 1,
            "%.*lf",    // restrict the number of digits
            col.scale_, // number of digits after the decimal point
            data);
        if (bytes == -1)
            throw TypeIncompatipleError();
        else if ((SQLULEN)bytes < columnSize)
            buffer.resize(bytes);
        buffer.resize(std::strlen(buffer.data())); // drop any trailing nulls
        Result.reserve(buffer.size() * sizeof(StringType::value_type));
        convert(buffer, Result);
        return;
    }

    case SQL_C_DATE:
    {
        const date d = *reinterpret_cast<date*>(col.pdata_ + rowset_position_ * col.clen_);
        std::tm st = {0};
        st.tm_year = d.year - 1900;
        st.tm_mon = d.month - 1;
        st.tm_mday = d.day;
        char* old_lc_time = std::setlocale(LC_TIME, nullptr);
        std::setlocale(LC_TIME, "");
        char date_str[512];
        std::strftime(date_str, sizeof(date_str), "%Y-%m-%d", &st);
        std::setlocale(LC_TIME, old_lc_time);
        convert(date_str, Result);
        return;
    }

    case SQL_C_TIME:
    {
        const time t = *reinterpret_cast<time*>(col.pdata_ + rowset_position_ * col.clen_);
        std::tm st = {0};
        st.tm_hour = t.hour;
        st.tm_min = t.min;
        st.tm_sec = t.sec;
        char* old_lc_time = std::setlocale(LC_TIME, nullptr);
        std::setlocale(LC_TIME, "");
        char date_str[512];
        std::strftime(date_str, sizeof(date_str), "%H:%M:%S", &st);
        std::setlocale(LC_TIME, old_lc_time);
        convert(date_str, Result);
        return;
    }

    case SQL_C_TIMESTAMP:
    {
        const timestamp stamp =
            *reinterpret_cast<timestamp*>(col.pdata_ + rowset_position_ * col.clen_);
        std::tm st = {0};
        st.tm_year = stamp.year - 1900;
        st.tm_mon = stamp.month - 1;
        st.tm_mday = stamp.day;
        st.tm_hour = stamp.hour;
        st.tm_min = stamp.min;
        st.tm_sec = stamp.sec;
        char* old_lc_time = std::setlocale(LC_TIME, nullptr);
        std::setlocale(LC_TIME, "");
        char date_str[512];
        std::strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S %z", &st);
        std::setlocale(LC_TIME, old_lc_time);
        convert(date_str, Result);
        return;
    }
    }
    throw TypeIncompatipleError();
}

template <>
inline void Result::ResultImpl::get_ref_impl<std::vector<std::uint8_t>>(
    short column,
    std::vector<std::uint8_t>& Result) const
{
    bound_column& col = bound_columns_[column];
    const SQLULEN columnSize = col.sqlsize_;

    switch (col.ctype_)
    {
    case SQL_C_BINARY:
    {
        if (col.blob_)
        {
            // Input and output is always array of bytes.
            std::vector<std::uint8_t> out;
            std::uint8_t buffer[1024] = {0};
            std::size_t const buffer_size = sizeof(buffer);
            // The length of the data available to return, decreasing with subsequent SQLGetData
            // calls.
            // But, NOT the length of data returned into the buffer (apart from the final call).
            SQLLEN ValueLenOrInd;
            SQLRETURN rc;

#if defined(NANODBC_DO_ASYNC_IMPL)
            stmt_.disableAsync();
#endif

            void* handle = nativeStatementHandle();
            do
            {
                NANODBC_CALL_RC(
                    SQLGetData,
                    rc,
                    handle,          // StatementHandle
                    column + 1,      // Col_or_Param_Num
                    SQL_C_BINARY,    // TargetType
                    buffer,          // TargetValuePtr
                    buffer_size,     // BufferLength
                    &ValueLenOrInd); // StrLen_or_IndPtr
                if (ValueLenOrInd > 0)
                {
                    auto const buffer_size_filled =
                        std::min<std::size_t>(ValueLenOrInd, buffer_size);
                    NANODBC_ASSERT(buffer_size_filled <= buffer_size);
                    out.insert(std::end(out), buffer, buffer + buffer_size_filled);
                }
                else if (ValueLenOrInd == SQL_NULL_DATA)
                    col.cbdata_[rowset_position_] = (SQLINTEGER)SQL_NULL_DATA;
                // Sequence of successful calls is:
                // SQL_NO_DATA or SQL_SUCCESS_WITH_INFO followed by SQL_SUCCESS.
            } while (rc == SQL_SUCCESS_WITH_INFO);
            if (rc == SQL_SUCCESS || rc == SQL_NO_DATA)
                Result = std::move(out);
            else if (!success(rc))
                NANODBC_THROW_DATABASE_ERROR(stmt_.nativeStatementHandle(), SQL_HANDLE_STMT);
        }
        else
        {
            // Read fixed-length binary data
            const char* s = col.pdata_ + rowset_position_ * col.clen_;
            Result.assign(s, s + columnSize);
        }
        return;
    }
    }
    throw TypeIncompatipleError();
}

template <class T>
void Result::ResultImpl::get_ref_impl(short column, T& Result) const
{
    bound_column& col = bound_columns_[column];
    using namespace std; // if int64_t is in std namespace (in c++11)
    const char* s = col.pdata_ + rowset_position_ * col.clen_;
    switch (col.ctype_)
    {
    case SQL_C_CHAR:
        Result = (T) * (char*)(s);
        return;
    case SQL_C_SSHORT:
        Result = (T) * (short*)(s);
        return;
    case SQL_C_USHORT:
        Result = (T) * (unsigned short*)(s);
        return;
    case SQL_C_LONG:
        Result = (T) * (int32_t*)(s);
        return;
    case SQL_C_SLONG:
        Result = (T) * (int32_t*)(s);
        return;
    case SQL_C_ULONG:
        Result = (T) * (uint32_t*)(s);
        return;
    case SQL_C_FLOAT:
        Result = (T) * (float*)(s);
        return;
    case SQL_C_DOUBLE:
        Result = (T) * (double*)(s);
        return;
    case SQL_C_SBIGINT:
        Result = (T) * (int64_t*)(s);
        return;
    case SQL_C_UBIGINT:
        Result = (T) * (uint64_t*)(s);
        return;
    }
    throw TypeIncompatipleError();
}

} // namespace Nanodbc

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

namespace Nanodbc
{

std::list<Driver> listDrivers()
{
    NANODBC_SQLCHAR descr[1024] = {0};
    NANODBC_SQLCHAR attrs[1024] = {0};
    SQLSMALLINT descr_len_ret{0};
    SQLSMALLINT attrs_len_ret{0};
    SQLUSMALLINT direction{SQL_FETCH_FIRST};

    HENV env{0};
    allocate_environment_handle(env);

    std::list<Driver> drivers;
    RETCODE rc{SQL_SUCCESS};
    do
    {
        NANODBC_ASSERT(env);
        NANODBC_CALL_RC(
            NANODBC_FUNC(SQLDrivers),
            rc,
            env,
            direction,                               // EnvironmentHandle
            descr,                                   // DriverDescription
            sizeof(descr) / sizeof(NANODBC_SQLCHAR), // BufferLength1
            &descr_len_ret,                          // DescriptionLengthPtr
            attrs,                                   // DriverAttributes
            sizeof(attrs) / sizeof(NANODBC_SQLCHAR), // BufferLength2
            &attrs_len_ret);                         // AttributesLengthPtr

        if (rc == SQL_SUCCESS)
        {
            using char_type = StringType::value_type;
            static_assert(
                sizeof(NANODBC_SQLCHAR) == sizeof(char_type),
                "incompatible SQLCHAR and StringType::value_type");

            Driver drv;
            drv.name = StringType(&descr[0], &descr[strarrlen(descr)]);

            // Split "Key1=Value1\0Key2=Value2\0\0" into list of key-value pairs
            auto beg = &attrs[0];
            auto const end = &attrs[attrs_len_ret];
            auto pair_end = end;
            while ((pair_end = std::find(beg, end, NANODBC_TEXT('\0'))) != end)
            {
                auto const eq_pos = std::find(beg, pair_end, NANODBC_TEXT('='));
                if (eq_pos == end)
                    break;

                Driver::Attribute attr{{beg, eq_pos}, {eq_pos + 1, pair_end}};
                drv.attributes.push_back(std::move(attr));
                beg = pair_end + 1;
            }

            drivers.push_back(std::move(drv));

            direction = SQL_FETCH_NEXT;
        }
        else
        {
            if (rc != SQL_NO_DATA)
                NANODBC_THROW_DATABASE_ERROR(env, SQL_HANDLE_ENV);
        }
    } while (success(rc));

    return drivers;
}

Result execute(Connection& conn, const StringType& query, long batchOperations, long timeout)
{
    class Statement Statement;
    return Statement.executeDirect(conn, query, batchOperations, timeout);
}

void justExecute(Connection& conn, const StringType& query, long batchOperations, long timeout)
{
    class Statement Statement;
    Statement.justExecuteDirect(conn, query, batchOperations, timeout);
}

Result execute(Statement& stmt, long batchOperations)
{
    return stmt.execute(batchOperations);
}

void justExecute(Statement& stmt, long batchOperations)
{
    return stmt.justExecute(batchOperations);
}

Result transact(Statement& stmt, long batchOperations)
{
    class Transaction Transaction(stmt.Connection());
    Result rvalue = stmt.execute(batchOperations);
    Transaction.commit();
    return rvalue;
}

void justTransact(Statement& stmt, long batchOperations)
{
    class Transaction Transaction(stmt.Connection());
    stmt.justExecute(batchOperations);
    Transaction.commit();
}

void prepare(Statement& stmt, const StringType& query, long timeout)
{
    stmt.prepare(stmt.Connection(), query, timeout);
}

} // namespace Nanodbc

// clang-format off
//  .d8888b.                                               888    d8b                             8888888888                 888
// d88P  Y88b                                              888    Y8P                             888                        888
// 888    888                                              888                                    888                        888
// 888         .d88b.  88888b.  88888b.   .d88b.   .d8888b 888888 888  .d88b.  88888b.            8888888 888  888  888  .d88888
// 888        d88""88b 888 "88b 888 "88b d8P  Y8b d88P"    888    888 d88""88b 888 "88b           888     888  888  888 d88" 888
// 888    888 888  888 888  888 888  888 88888888 888      888    888 888  888 888  888           888     888  888  888 888  888
// Y88b  d88P Y88..88P 888  888 888  888 Y8b.     Y88b.    Y88b.  888 Y88..88P 888  888           888     Y88b 888 d88P Y88b 888
//  "Y8888P"   "Y88P"  888  888 888  888  "Y8888   "Y8888P  "Y888 888  "Y88P"  888  888           888      "Y8888888P"   "Y88888
// MARK: Connection Fwd -
// clang-format on

namespace Nanodbc
{

Connection::Connection()
    : impl_(new ConnectionImpl())
{
}

Connection::Connection(const Connection& rhs)
    : impl_(rhs.impl_)
{
}

#ifndef NANODBC_NO_MOVE_CTOR
Connection::Connection(Connection&& rhs) NANODBC_NOEXCEPT : impl_(std::move(rhs.impl_))
{
}
#endif

Connection& Connection::operator=(Connection rhs)
{
    swap(rhs);
    return *this;
}

void Connection::swap(Connection& rhs) NANODBC_NOEXCEPT
{
    using std::swap;
    swap(impl_, rhs.impl_);
}

Connection::Connection(
    const StringType& dsn,
    const StringType& user,
    const StringType& pass,
    long timeout)
    : impl_(new ConnectionImpl(dsn, user, pass, timeout))
{
}

Connection::Connection(const StringType& connectionString, long timeout)
    : impl_(new ConnectionImpl(connectionString, timeout))
{
}

Connection::~Connection() NANODBC_NOEXCEPT
{
}

void Connection::connect(
    const StringType& dsn,
    const StringType& user,
    const StringType& pass,
    long timeout)
{
    impl_->connect(dsn, user, pass, timeout);
}

void Connection::connect(const StringType& connectionString, long timeout)
{
    impl_->connect(connectionString, timeout);
}

#if !defined(NANODBC_DISABLE_ASYNC) && defined(SQL_ATTR_ASYNC_DBC_EVENT)
bool Connection::async_connect(
    const StringType& dsn,
    const StringType& user,
    const StringType& pass,
    void* eventHandle,
    long timeout)
{
    return impl_->connect(dsn, user, pass, timeout, eventHandle) == SQL_STILL_EXECUTING;
}

bool Connection::async_connect(
    const StringType& connectionString,
    void* eventHandle,
    long timeout)
{
    return impl_->connect(connectionString, timeout, eventHandle) == SQL_STILL_EXECUTING;
}

void Connection::asyncComplete()
{
    impl_->asyncComplete();
}
#endif // !NANODBC_DISABLE_ASYNC && SQL_ATTR_ASYNC_DBC_EVENT

bool Connection::connected() const
{
    return impl_->connected();
}

void Connection::disconnect()
{
    impl_->disconnect();
}

std::size_t Connection::transactions() const
{
    return impl_->transactions();
}

void* Connection::nativeDbcHandle() const
{
    return impl_->nativeDbcHandle();
}

void* Connection::nativeEnvHandle() const
{
    return impl_->nativeEnvHandle();
}

StringType Connection::dbmsName() const
{
    return impl_->dbmsName();
}

StringType Connection::dbmsVersion() const
{
    return impl_->dbmsVersion();
}

StringType Connection::driverName() const
{
    return impl_->driverName();
}

StringType Connection::databaseName() const
{
    return impl_->databaseName();
}

StringType Connection::catalogName() const
{
    return impl_->catalogName();
}

std::size_t Connection::refTransaction()
{
    return impl_->refTransaction();
}

std::size_t Connection::unrefTransaction()
{
    return impl_->unrefTransaction();
}

bool Connection::rollback() const
{
    return impl_->rollback();
}

void Connection::rollback(bool onoff)
{
    impl_->rollback(onoff);
}

} // namespace Nanodbc

// clang-format off
// 88888888888                                                  888    d8b                             8888888888                 888
//     888                                                      888    Y8P                             888                        888
//     888                                                      888                                    888                        888
//     888  888d888 8888b.  88888b.  .d8888b   8888b.   .d8888b 888888 888  .d88b.  88888b.            8888888 888  888  888  .d88888 .d8888b
//     888  888P"      "88b 888 "88b 88K          "88b d88P"    888    888 d88""88b 888 "88b           888     888  888  888 d88" 888 88K
//     888  888    .d888888 888  888 "Y8888b. .d888888 888      888    888 888  888 888  888           888     888  888  888 888  888 "Y8888b.
//     888  888    888  888 888  888      X88 888  888 Y88b.    Y88b.  888 Y88..88P 888  888           888     Y88b 888 d88P Y88b 888      X88
//     888  888    "Y888888 888  888  88888P' "Y888888  "Y8888P  "Y888 888  "Y88P"  888  888           888      "Y8888888P"   "Y88888  88888P'
// MARK: Transaction Fwd -
// clang-format on

namespace Nanodbc
{

Transaction::Transaction(const class Connection& conn)
    : impl_(new TransactionImpl(conn))
{
}

Transaction::Transaction(const Transaction& rhs)
    : impl_(rhs.impl_)
{
}

#ifndef NANODBC_NO_MOVE_CTOR
Transaction::Transaction(Transaction&& rhs) NANODBC_NOEXCEPT : impl_(std::move(rhs.impl_))
{
}
#endif

Transaction& Transaction::operator=(Transaction rhs)
{
    swap(rhs);
    return *this;
}

void Transaction::swap(Transaction& rhs) NANODBC_NOEXCEPT
{
    using std::swap;
    swap(impl_, rhs.impl_);
}

Transaction::~Transaction() NANODBC_NOEXCEPT
{
}

void Transaction::commit()
{
    impl_->commit();
}

void Transaction::rollback() NANODBC_NOEXCEPT
{
    impl_->rollback();
}

class Connection& Transaction::Connection()
{
    return impl_->Connection();
}

const class Connection& Transaction::Connection() const
{
    return impl_->Connection();
}

Transaction::operator class Connection&()
{
    return impl_->Connection();
}

Transaction::operator const class Connection&() const
{
    return impl_->Connection();
}

} // namespace Nanodbc

// clang-format off
//  .d8888b.  888             888                                            888              8888888888                 888
// d88P  Y88b 888             888                                            888              888                        888
// Y88b.      888             888                                            888              888                        888
//  "Y888b.   888888  8888b.  888888 .d88b.  88888b.d88b.   .d88b.  88888b.  888888           8888888 888  888  888  .d88888
//     "Y88b. 888        "88b 888   d8P  Y8b 888 "888 "88b d8P  Y8b 888 "88b 888              888     888  888  888 d88" 888
//       "888 888    .d888888 888   88888888 888  888  888 88888888 888  888 888              888     888  888  888 888  888
// Y88b  d88P Y88b.  888  888 Y88b. Y8b.     888  888  888 Y8b.     888  888 Y88b.            888     Y88b 888 d88P Y88b 888
//  "Y8888P"   "Y888 "Y888888  "Y888 "Y8888  888  888  888  "Y8888  888  888  "Y888           888      "Y8888888P"   "Y88888
// MARK: Statement Fwd -
// clang-format on

namespace Nanodbc
{

Statement::Statement()
    : impl_(new StatementImpl())
{
}

Statement::Statement(class Connection& conn)
    : impl_(new StatementImpl(conn))
{
}

#ifndef NANODBC_NO_MOVE_CTOR
Statement::Statement(Statement&& rhs) NANODBC_NOEXCEPT : impl_(std::move(rhs.impl_))
{
}
#endif

Statement::Statement(class Connection& conn, const StringType& query, long timeout)
    : impl_(new StatementImpl(conn, query, timeout))
{
}

Statement::Statement(const Statement& rhs)
    : impl_(rhs.impl_)
{
}

Statement& Statement::operator=(Statement rhs)
{
    swap(rhs);
    return *this;
}

void Statement::swap(Statement& rhs) NANODBC_NOEXCEPT
{
    using std::swap;
    swap(impl_, rhs.impl_);
}

Statement::~Statement() NANODBC_NOEXCEPT
{
}

void Statement::open(class Connection& conn)
{
    impl_->open(conn);
}

bool Statement::open() const
{
    return impl_->open();
}

bool Statement::connected() const
{
    return impl_->connected();
}

const class Connection& Statement::Connection() const
{
    return impl_->Connection();
}

class Connection& Statement::Connection()
{
    return impl_->Connection();
}

void* Statement::nativeStatementHandle() const
{
    return impl_->nativeStatementHandle();
}

void Statement::close()
{
    impl_->close();
}

void Statement::cancel()
{
    impl_->cancel();
}

void Statement::prepare(class Connection& conn, const StringType& query, long timeout)
{
    impl_->prepare(conn, query, timeout);
}

void Statement::prepare(const StringType& query, long timeout)
{
    impl_->prepare(query, timeout);
}

void Statement::timeout(long timeout)
{
    impl_->timeout(timeout);
}

Result Statement::executeDirect(
    class Connection& conn,
    const StringType& query,
    long batchOperations,
    long timeout)
{
    return impl_->executeDirect(conn, query, batchOperations, timeout, *this);
}

#if defined(NANODBC_DO_ASYNC_IMPL)
bool Statement::asyncPrepare(const StringType& query, void* eventHandle, long timeout)
{
    return impl_->asyncPrepare(query, eventHandle, timeout);
}

bool Statement::asyncExecuteDirect(
    class Connection& conn,
    void* eventHandle,
    const StringType& query,
    long batchOperations,
    long timeout)
{
    return impl_->asyncExecuteDirect(conn, eventHandle, query, batchOperations, timeout, *this);
}

bool Statement::async_execute(void* eventHandle, long batchOperations, long timeout)
{
    return impl_->async_execute(eventHandle, batchOperations, timeout, *this);
}

void Statement::completePrepare()
{
    return impl_->completePrepare();
}

Result Statement::complete_execute(long batchOperations)
{
    return impl_->complete_execute(batchOperations, *this);
}

Result Statement::asyncComplete(long batchOperations)
{
    return impl_->complete_execute(batchOperations, *this);
}

void Statement::enableAsync(void* eventHandle)
{
    impl_->enableAsync(eventHandle);
}

void Statement::disableAsync() const
{
    impl_->disableAsync();
}
#endif

void Statement::justExecuteDirect(
    class Connection& conn,
    const StringType& query,
    long batchOperations,
    long timeout)
{
    impl_->justExecuteDirect(conn, query, batchOperations, timeout, *this);
}

Result Statement::execute(long batchOperations, long timeout)
{
    return impl_->execute(batchOperations, timeout, *this);
}

void Statement::justExecute(long batchOperations, long timeout)
{
    impl_->justExecute(batchOperations, timeout, *this);
}

Result Statement::procedure_columns(
    const StringType& Catalog,
    const StringType& schema,
    const StringType& procedure,
    const StringType& column)
{
    return impl_->procedure_columns(Catalog, schema, procedure, column, *this);
}

long Statement::affectedRows() const
{
    return impl_->affectedRows();
}

short Statement::columns() const
{
    return impl_->columns();
}

void Statement::resetParameters() NANODBC_NOEXCEPT
{
    impl_->resetParameters();
}

unsigned long Statement::parameterSize(short param) const
{
    return impl_->parameterSize(param);
}

// We need to instantiate each form of bind() for each of our supported data types.
#define NANODBC_INSTANTIATE_BINDS(type)                                                            \
    template void Statement::bind(short, const type*, ParamDirection);              /* 1-ary */   \
    template void Statement::bind(short, const type*, std::size_t, ParamDirection); /* n-ary */   \
    template void Statement::bind(                                                                 \
        short, const type*, std::size_t, const type*, ParamDirection); /* n-ary, sentry */        \
    template void Statement::bind(                                                                 \
        short, const type*, std::size_t, const bool*, ParamDirection) /* n-ary, flags */ /**/

// The following are the only supported instantiations of Statement::bind().
NANODBC_INSTANTIATE_BINDS(StringType::value_type);
NANODBC_INSTANTIATE_BINDS(short);
NANODBC_INSTANTIATE_BINDS(unsigned short);
NANODBC_INSTANTIATE_BINDS(int32_t);
NANODBC_INSTANTIATE_BINDS(uint32_t);
NANODBC_INSTANTIATE_BINDS(int64_t);
NANODBC_INSTANTIATE_BINDS(uint64_t);
NANODBC_INSTANTIATE_BINDS(float);
NANODBC_INSTANTIATE_BINDS(double);
NANODBC_INSTANTIATE_BINDS(date);
NANODBC_INSTANTIATE_BINDS(time);
NANODBC_INSTANTIATE_BINDS(timestamp);

#undef NANODBC_INSTANTIATE_BINDS

template <class T>
void Statement::bind(short param, const T* value, ParamDirection direction)
{
    impl_->bind(param, value, 1, direction);
}

template <class T>
void Statement::bind(short param, const T* values, std::size_t elements, ParamDirection direction)
{
    impl_->bind(param, values, elements, direction);
}

template <class T>
void Statement::bind(
    short param,
    const T* values,
    std::size_t elements,
    const T* nullSentry,
    ParamDirection direction)
{
    impl_->bind(param, values, elements, 0, nullSentry, direction);
}

template <class T>
void Statement::bind(
    short param,
    const T* values,
    std::size_t elements,
    const bool* nulls,
    ParamDirection direction)
{
    impl_->bind(param, values, elements, nulls, (T*)0, direction);
}

void Statement::bindStrings(
    short param,
    const StringType::value_type* values,
    std::size_t length,
    std::size_t elements,
    ParamDirection direction)
{
    impl_->bindStrings(param, values, length, elements, direction);
}

void Statement::bindStrings(
    short param,
    const StringType::value_type* values,
    std::size_t length,
    std::size_t elements,
    const StringType::value_type* nullSentry,
    ParamDirection direction)
{
    impl_->bindStrings(param, values, length, elements, (bool*)0, nullSentry, direction);
}

void Statement::bindStrings(
    short param,
    const StringType::value_type* values,
    std::size_t length,
    std::size_t elements,
    const bool* nulls,
    ParamDirection direction)
{
    impl_->bindStrings(
        param, values, length, elements, nulls, (StringType::value_type*)0, direction);
}

void Statement::bind_null(short param, std::size_t elements)
{
    impl_->bind_null(param, elements);
}

} // namespace Nanodbc

namespace Nanodbc
{

Catalog::Tables::Tables(Result& findResult)
    : result_(findResult)
{
}

bool Catalog::Tables::next()
{
    return result_.next();
}

StringType Catalog::Tables::tableCatalog() const
{
    // TABLE_CAT might be NULL
    return result_.get<StringType>(0, StringType());
}

StringType Catalog::Tables::tableSchema() const
{
    // TABLE_SCHEM might be NULL
    return result_.get<StringType>(1, StringType());
}

StringType Catalog::Tables::tableName() const
{
    // TABLE_NAME column is never NULL
    return result_.get<StringType>(2);
}

StringType Catalog::Tables::tableType() const
{
    // TABLE_TYPE column is never NULL
    return result_.get<StringType>(3);
}

StringType Catalog::Tables::TableRemarks() const
{
    // REMARKS might be NULL
    return result_.get<StringType>(4, StringType());
}

Catalog::TablePrivileges::TablePrivileges(Result& findResult)
    : result_(findResult)
{
}

bool Catalog::TablePrivileges::next()
{
    return result_.next();
}

StringType Catalog::TablePrivileges::tableCatalog() const
{
    // TABLE_CAT might be NULL
    return result_.get<StringType>(0, StringType());
}

StringType Catalog::TablePrivileges::tableSchema() const
{
    // TABLE_SCHEM might be NULL
    return result_.get<StringType>(1, StringType());
}

StringType Catalog::TablePrivileges::tableName() const
{
    // TABLE_NAME column is never NULL
    return result_.get<StringType>(2);
}

StringType Catalog::TablePrivileges::grantor() const
{
    // GRANTOR might be NULL
    return result_.get<StringType>(3, StringType());
}

StringType Catalog::TablePrivileges::grantee() const
{
    // GRANTEE column is never NULL
    return result_.get<StringType>(4);
}

StringType Catalog::TablePrivileges::privilege() const
{
    // PRIVILEGE column is never NULL
    return result_.get<StringType>(5);
}

StringType Catalog::TablePrivileges::isGrantable() const
{
    // IS_GRANTABLE might be NULL
    return result_.get<StringType>(6, StringType());
}

Catalog::PrimaryKeys::PrimaryKeys(Result& findResult)
    : result_(findResult)
{
}

bool Catalog::PrimaryKeys::next()
{
    return result_.next();
}

StringType Catalog::PrimaryKeys::tableCatalog() const
{
    // TABLE_CAT might be NULL
    return result_.get<StringType>(0, StringType());
}

StringType Catalog::PrimaryKeys::tableSchema() const
{
    // TABLE_SCHEM might be NULL
    return result_.get<StringType>(1, StringType());
}

StringType Catalog::PrimaryKeys::tableName() const
{
    // TABLE_NAME is never NULL
    return result_.get<StringType>(2);
}

StringType Catalog::PrimaryKeys::columnName() const
{
    // COLUMN_NAME is never NULL
    return result_.get<StringType>(3);
}

short Catalog::PrimaryKeys::columnNumber() const
{
    // KEY_SEQ is never NULL
    return result_.get<short>(4);
}

StringType Catalog::PrimaryKeys::primaryKeyName() const
{
    // PK_NAME might be NULL
    return result_.get<StringType>(5);
}

Catalog::columns::columns(Result& findResult)
    : result_(findResult)
{
}

bool Catalog::columns::next()
{
    return result_.next();
}

StringType Catalog::columns::tableCatalog() const
{
    // TABLE_CAT might be NULL
    return result_.get<StringType>(0, StringType());
}

StringType Catalog::columns::tableSchema() const
{
    // TABLE_SCHEM might be NULL
    return result_.get<StringType>(1, StringType());
}

StringType Catalog::columns::tableName() const
{
    // TABLE_NAME is never NULL
    return result_.get<StringType>(2);
}

StringType Catalog::columns::columnName() const
{
    // COLUMN_NAME is never NULL
    return result_.get<StringType>(3);
}

short Catalog::columns::dataType() const
{
    // DATA_TYPE is never NULL
    return result_.get<short>(4);
}

StringType Catalog::columns::typeName() const
{
    // TYPE_NAME is never NULL
    return result_.get<StringType>(5);
}

long Catalog::columns::columnSize() const
{
    // COLUMN_SIZE
    return result_.get<long>(6);
}

long Catalog::columns::bufferLength() const
{
    // BUFFER_LENGTH
    return result_.get<long>(7);
}

short Catalog::columns::decimalDigits() const
{
    // DECIMAL_DIGITS might be NULL
    return result_.get<short>(8, 0);
}

short Catalog::columns::numericPrecisionRadix() const
{
    // NUM_PREC_RADIX might be NULL
    return result_.get<short>(9, 0);
}

short Catalog::columns::nullable() const
{
    // NULLABLE is never NULL
    return result_.get<short>(10);
}

StringType Catalog::columns::remarks() const
{
    // REMARKS might be NULL
    return result_.get<StringType>(11, StringType());
}

StringType Catalog::columns::columnDefault() const
{
    // COLUMN_DEF might be NULL, if no default value is specified
    return result_.get<StringType>(12, StringType());
}

short Catalog::columns::sqlDataType() const
{
    // SQL_DATA_TYPE is never NULL
    return result_.get<short>(13);
}

short Catalog::columns::sqlDatetimeSubtype() const
{
    // SQL_DATETIME_SUB might be NULL
    return result_.get<short>(14, 0);
}

long Catalog::columns::charOctetLength() const
{
    // CHAR_OCTET_LENGTH might be NULL
    return result_.get<long>(15, 0);
}

long Catalog::columns::ordinalPosition() const
{
    // ORDINAL_POSITION is never NULL
    return result_.get<long>(16);
}

StringType Catalog::columns::isNullable() const
{
    // IS_NULLABLE might be NULL.
    return result_.get<StringType>(17, StringType());
}

Catalog::Catalog(Connection& conn)
    : conn_(conn)
{
}

Catalog::Tables Catalog::find_tables(
    const StringType& table,
    const StringType& type,
    const StringType& schema,
    const StringType& Catalog)
{
    // Passing a null pointer to a search pattern argument does not
    // constrain the search for that argument; that is, a null pointer and
    // the search pattern % (any characters) are equivalent.
    // However, a zero-length search pattern - that is, a valid pointer to
    // a string of length zero - matches only the empty string ("").
    // See https://msdn.microsoft.com/en-us/library/ms710171.aspx

    Statement stmt(conn_);
    RETCODE rc;
    NANODBC_CALL_RC(
        NANODBC_FUNC(SQLTables),
        rc,
        stmt.nativeStatementHandle(),
        (NANODBC_SQLCHAR*)(Catalog.empty() ? nullptr : Catalog.c_str()),
        (Catalog.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(schema.empty() ? nullptr : schema.c_str()),
        (schema.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(table.empty() ? nullptr : table.c_str()),
        (table.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(type.empty() ? nullptr : type.c_str()),
        (type.empty() ? 0 : SQL_NTS));
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt.nativeStatementHandle(), SQL_HANDLE_STMT);

    Result findResult(stmt, 1);
    return Catalog::Tables(findResult);
}

Catalog::TablePrivileges Catalog::findTablePrivileges(
    const StringType& Catalog,
    const StringType& table,
    const StringType& schema)
{
    // Passing a null pointer to a search pattern argument does not
    // constrain the search for that argument; that is, a null pointer and
    // the search pattern % (any characters) are equivalent.
    // However, a zero-length search pattern - that is, a valid pointer to
    // a string of length zero - matches only the empty string ("").
    // See https://msdn.microsoft.com/en-us/library/ms710171.aspx

    Statement stmt(conn_);
    RETCODE rc;
    NANODBC_CALL_RC(
        NANODBC_FUNC(SQLTablePrivileges),
        rc,
        stmt.nativeStatementHandle(),
        (NANODBC_SQLCHAR*)(Catalog.empty() ? nullptr : Catalog.c_str()),
        (Catalog.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(schema.empty() ? nullptr : schema.c_str()),
        (schema.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(table.empty() ? nullptr : table.c_str()),
        (table.empty() ? 0 : SQL_NTS));
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt.nativeStatementHandle(), SQL_HANDLE_STMT);

    Result findResult(stmt, 1);
    return Catalog::TablePrivileges(findResult);
}

Catalog::columns Catalog::findColumns(
    const StringType& column,
    const StringType& table,
    const StringType& schema,
    const StringType& Catalog)
{
    Statement stmt(conn_);
    RETCODE rc;
    NANODBC_CALL_RC(
        NANODBC_FUNC(SQLColumns),
        rc,
        stmt.nativeStatementHandle(),
        (NANODBC_SQLCHAR*)(Catalog.empty() ? nullptr : Catalog.c_str()),
        (Catalog.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(schema.empty() ? nullptr : schema.c_str()),
        (schema.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(table.empty() ? nullptr : table.c_str()),
        (table.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(column.empty() ? nullptr : column.c_str()),
        (column.empty() ? 0 : SQL_NTS));
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt.nativeStatementHandle(), SQL_HANDLE_STMT);

    Result findResult(stmt, 1);
    return Catalog::columns(findResult);
}

Catalog::PrimaryKeys Catalog::findPrimaryKeys(
    const StringType& table,
    const StringType& schema,
    const StringType& Catalog)
{
    Statement stmt(conn_);
    RETCODE rc;
    NANODBC_CALL_RC(
        NANODBC_FUNC(SQLPrimaryKeys),
        rc,
        stmt.nativeStatementHandle(),
        (NANODBC_SQLCHAR*)(Catalog.empty() ? nullptr : Catalog.c_str()),
        (Catalog.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(schema.empty() ? nullptr : schema.c_str()),
        (schema.empty() ? 0 : SQL_NTS),
        (NANODBC_SQLCHAR*)(table.empty() ? nullptr : table.c_str()),
        (table.empty() ? 0 : SQL_NTS));
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt.nativeStatementHandle(), SQL_HANDLE_STMT);

    Result findResult(stmt, 1);
    return Catalog::PrimaryKeys(findResult);
}

std::list<StringType> Catalog::listCatalogs()
{
    // Special case for list of catalogs only:
    // all the other arguments must match empty string (""),
    // otherwise pattern-based lookup is performed returning
    // Cartesian product of catalogs, Tables and schemas.
    Statement stmt(conn_);
    RETCODE rc;
    NANODBC_CALL_RC(
        NANODBC_FUNC(SQLTables),
        rc,
        stmt.nativeStatementHandle(),
        (NANODBC_SQLCHAR*)SQL_ALL_CATALOGS,
        1,
        (NANODBC_SQLCHAR*)NANODBC_TEXT(""),
        0,
        (NANODBC_SQLCHAR*)NANODBC_TEXT(""),
        0,
        (NANODBC_SQLCHAR*)NANODBC_TEXT(""),
        0);
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt.nativeStatementHandle(), SQL_HANDLE_STMT);

    Result findResult(stmt, 1);
    Catalog::Tables catalogs(findResult);

    std::list<StringType> names;
    while (catalogs.next())
        names.push_back(catalogs.tableCatalog());
    return names;
}

std::list<StringType> Catalog::listSchemas()
{
    // TODO: Possible to restrict list of schemas from a specified Catalog?

    // Special case for list of schemas:
    // all the other arguments must match empty string (""),
    // otherwise pattern-based lookup is performed returning
    // Cartesian product of catalogs, Tables and schemas.
    Statement stmt(conn_);
    RETCODE rc;
    NANODBC_CALL_RC(
        NANODBC_FUNC(SQLTables),
        rc,
        stmt.nativeStatementHandle(),
        (NANODBC_SQLCHAR*)NANODBC_TEXT(""),
        0,
        (NANODBC_SQLCHAR*)SQL_ALL_SCHEMAS,
        1,
        (NANODBC_SQLCHAR*)NANODBC_TEXT(""),
        0,
        (NANODBC_SQLCHAR*)NANODBC_TEXT(""),
        0);
    if (!success(rc))
        NANODBC_THROW_DATABASE_ERROR(stmt.nativeStatementHandle(), SQL_HANDLE_STMT);

    Result findResult(stmt, 1);
    Catalog::Tables schemas(findResult);

    std::list<StringType> names;
    while (schemas.next())
        names.push_back(schemas.tableSchema());
    return names;
}

} // namespace Nanodbc

// clang-format off
// 8888888b.                            888 888              8888888888                 888
// 888   Y88b                           888 888              888                        888
// 888    888                           888 888              888                        888
// 888   d88P .d88b.  .d8888b  888  888 888 888888           8888888 888  888  888  .d88888
// 8888888P" d8P  Y8b 88K      888  888 888 888              888     888  888  888 d88" 888
// 888 T88b  88888888 "Y8888b. 888  888 888 888              888     888  888  888 888  888
// 888  T88b Y8b.          X88 Y88b 888 888 Y88b.            888     Y88b 888 d88P Y88b 888
// 888   T88b "Y8888   88888P'  "Y88888 888  "Y888           888      "Y8888888P"   "Y88888
// MARK: Result Fwd -
// clang-format on

namespace Nanodbc
{

Result::Result()
    : impl_()
{
}

Result::~Result() NANODBC_NOEXCEPT
{
}

Result::Result(Statement stmt, long rowsetSize)
    : impl_(new ResultImpl(stmt, rowsetSize))
{
}

#ifndef NANODBC_NO_MOVE_CTOR
Result::Result(Result&& rhs) NANODBC_NOEXCEPT : impl_(std::move(rhs.impl_))
{
}
#endif

Result::Result(const Result& rhs)
    : impl_(rhs.impl_)
{
}

Result& Result::operator=(Result rhs)
{
    swap(rhs);
    return *this;
}

void Result::swap(Result& rhs) NANODBC_NOEXCEPT
{
    using std::swap;
    swap(impl_, rhs.impl_);
}

void* Result::nativeStatementHandle() const
{
    return impl_->nativeStatementHandle();
}

long Result::rowsetSize() const NANODBC_NOEXCEPT
{
    return impl_->rowsetSize();
}

long Result::affectedRows() const
{
    return impl_->affectedRows();
}

long Result::rows() const NANODBC_NOEXCEPT
{
    return impl_->rows();
}

short Result::columns() const
{
    return impl_->columns();
}

bool Result::first()
{
    return impl_->first();
}

bool Result::last()
{
    return impl_->last();
}

bool Result::next()
{
    return impl_->next();
}

#if defined(NANODBC_DO_ASYNC_IMPL)
bool Result::asyncNext(void* eventHandle)
{
    return impl_->asyncNext(eventHandle);
}

bool Result::completeNext()
{
    return impl_->completeNext();
}
#endif

bool Result::prior()
{
    return impl_->prior();
}

bool Result::move(long row)
{
    return impl_->move(row);
}

bool Result::skip(long rows)
{
    return impl_->skip(rows);
}

unsigned long Result::position() const
{
    return impl_->position();
}

bool Result::atEnd() const NANODBC_NOEXCEPT
{
    return impl_->atEnd();
}

bool Result::isNull(short column) const
{
    return impl_->isNull(column);
}

bool Result::isNull(const StringType& columnName) const
{
    return impl_->isNull(columnName);
}

short Result::column(const StringType& columnName) const
{
    return impl_->column(columnName);
}

StringType Result::columnName(short column) const
{
    return impl_->columnName(column);
}

long Result::columnSize(short column) const
{
    return impl_->columnSize(column);
}

long Result::columnSize(const StringType& columnName) const
{
    return impl_->columnSize(columnName);
}

int Result::columnDecimalDigits(short column) const
{
    return impl_->columnDecimalDigits(column);
}

int Result::columnDecimalDigits(const StringType& columnName) const
{
    return impl_->columnDecimalDigits(columnName);
}

int Result::columnDatatype(short column) const
{
    return impl_->columnDatatype(column);
}

int Result::columnDatatype(const StringType& columnName) const
{
    return impl_->columnDatatype(columnName);
}

int Result::columnCDatatype(short column) const
{
    return impl_->columnCDatatype(column);
}

int Result::columnCDatatype(const StringType& columnName) const
{
    return impl_->columnCDatatype(columnName);
}

bool Result::nextResult()
{
    return impl_->nextResult();
}

template <class T>
void Result::getRef(short column, T& Result) const
{
    return impl_->getRef<T>(column, Result);
}

template <class T>
void Result::getRef(short column, const T& fallback, T& Result) const
{
    return impl_->getRef<T>(column, fallback, Result);
}

template <class T>
void Result::getRef(const StringType& columnName, T& Result) const
{
    return impl_->getRef<T>(columnName, Result);
}

template <class T>
void Result::getRef(const StringType& columnName, const T& fallback, T& Result) const
{
    return impl_->getRef<T>(columnName, fallback, Result);
}

template <class T>
T Result::get(short column) const
{
    return impl_->get<T>(column);
}

template <class T>
T Result::get(short column, const T& fallback) const
{
    return impl_->get<T>(column, fallback);
}

template <class T>
T Result::get(const StringType& columnName) const
{
    return impl_->get<T>(columnName);
}

template <class T>
T Result::get(const StringType& columnName, const T& fallback) const
{
    return impl_->get<T>(columnName, fallback);
}

Result::operator bool() const
{
    return static_cast<bool>(impl_);
}

// The following are the only supported instantiations of Result::getRef().
template void Result::getRef(short, StringType::value_type&) const;
template void Result::getRef(short, short&) const;
template void Result::getRef(short, unsigned short&) const;
template void Result::getRef(short, int32_t&) const;
template void Result::getRef(short, uint32_t&) const;
template void Result::getRef(short, int64_t&) const;
template void Result::getRef(short, uint64_t&) const;
template void Result::getRef(short, float&) const;
template void Result::getRef(short, double&) const;
template void Result::getRef(short, StringType&) const;
template void Result::getRef(short, date&) const;
template void Result::getRef(short, time&) const;
template void Result::getRef(short, timestamp&) const;
template void Result::getRef(short, std::vector<std::uint8_t>&) const;

template void Result::getRef(const StringType&, StringType::value_type&) const;
template void Result::getRef(const StringType&, short&) const;
template void Result::getRef(const StringType&, unsigned short&) const;
template void Result::getRef(const StringType&, int32_t&) const;
template void Result::getRef(const StringType&, uint32_t&) const;
template void Result::getRef(const StringType&, int64_t&) const;
template void Result::getRef(const StringType&, uint64_t&) const;
template void Result::getRef(const StringType&, float&) const;
template void Result::getRef(const StringType&, double&) const;
template void Result::getRef(const StringType&, StringType&) const;
template void Result::getRef(const StringType&, date&) const;
template void Result::getRef(const StringType&, time&) const;
template void Result::getRef(const StringType&, timestamp&) const;
template void Result::getRef(const StringType&, std::vector<std::uint8_t>&) const;

// The following are the only supported instantiations of Result::getRef() with fallback.
template void
Result::getRef(short, const StringType::value_type&, StringType::value_type&) const;
template void Result::getRef(short, const short&, short&) const;
template void Result::getRef(short, const unsigned short&, unsigned short&) const;
template void Result::getRef(short, const int32_t&, int32_t&) const;
template void Result::getRef(short, const uint32_t&, uint32_t&) const;
template void Result::getRef(short, const int64_t&, int64_t&) const;
template void Result::getRef(short, const uint64_t&, uint64_t&) const;
template void Result::getRef(short, const float&, float&) const;
template void Result::getRef(short, const double&, double&) const;
template void Result::getRef(short, const StringType&, StringType&) const;
template void Result::getRef(short, const date&, date&) const;
template void Result::getRef(short, const time&, time&) const;
template void Result::getRef(short, const timestamp&, timestamp&) const;
template void
Result::getRef(short, const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&) const;

template void
Result::getRef(const StringType&, const StringType::value_type&, StringType::value_type&) const;
template void Result::getRef(const StringType&, const short&, short&) const;
template void Result::getRef(const StringType&, const unsigned short&, unsigned short&) const;
template void Result::getRef(const StringType&, const int32_t&, int32_t&) const;
template void Result::getRef(const StringType&, const uint32_t&, uint32_t&) const;
template void Result::getRef(const StringType&, const int64_t&, int64_t&) const;
template void Result::getRef(const StringType&, const uint64_t&, uint64_t&) const;
template void Result::getRef(const StringType&, const float&, float&) const;
template void Result::getRef(const StringType&, const double&, double&) const;
template void Result::getRef(const StringType&, const StringType&, StringType&) const;
template void Result::getRef(const StringType&, const date&, date&) const;
template void Result::getRef(const StringType&, const time&, time&) const;
template void Result::getRef(const StringType&, const timestamp&, timestamp&) const;
template void Result::getRef(
    const StringType&,
    const std::vector<std::uint8_t>&,
    std::vector<std::uint8_t>&) const;

// The following are the only supported instantiations of Result::get().
template StringType::value_type Result::get(short) const;
template short Result::get(short) const;
template unsigned short Result::get(short) const;
template int32_t Result::get(short) const;
template uint32_t Result::get(short) const;
template int64_t Result::get(short) const;
template uint64_t Result::get(short) const;
template float Result::get(short) const;
template double Result::get(short) const;
template StringType Result::get(short) const;
template date Result::get(short) const;
template time Result::get(short) const;
template timestamp Result::get(short) const;
template std::vector<std::uint8_t> Result::get(short) const;

template StringType::value_type Result::get(const StringType&) const;
template short Result::get(const StringType&) const;
template unsigned short Result::get(const StringType&) const;
template int32_t Result::get(const StringType&) const;
template uint32_t Result::get(const StringType&) const;
template int64_t Result::get(const StringType&) const;
template uint64_t Result::get(const StringType&) const;
template float Result::get(const StringType&) const;
template double Result::get(const StringType&) const;
template StringType Result::get(const StringType&) const;
template date Result::get(const StringType&) const;
template time Result::get(const StringType&) const;
template timestamp Result::get(const StringType&) const;
template std::vector<std::uint8_t> Result::get(const StringType&) const;

// The following are the only supported instantiations of Result::get() with fallback.
template StringType::value_type Result::get(short, const StringType::value_type&) const;
template short Result::get(short, const short&) const;
template unsigned short Result::get(short, const unsigned short&) const;
template int32_t Result::get(short, const int32_t&) const;
template uint32_t Result::get(short, const uint32_t&) const;
template int64_t Result::get(short, const int64_t&) const;
template uint64_t Result::get(short, const uint64_t&) const;
template float Result::get(short, const float&) const;
template double Result::get(short, const double&) const;
template StringType Result::get(short, const StringType&) const;
template date Result::get(short, const date&) const;
template time Result::get(short, const time&) const;
template timestamp Result::get(short, const timestamp&) const;
template std::vector<std::uint8_t> Result::get(short, const std::vector<std::uint8_t>&) const;

template StringType::value_type
Result::get(const StringType&, const StringType::value_type&) const;
template short Result::get(const StringType&, const short&) const;
template unsigned short Result::get(const StringType&, const unsigned short&) const;
template int32_t Result::get(const StringType&, const int32_t&) const;
template uint32_t Result::get(const StringType&, const uint32_t&) const;
template int64_t Result::get(const StringType&, const int64_t&) const;
template uint64_t Result::get(const StringType&, const uint64_t&) const;
template float Result::get(const StringType&, const float&) const;
template double Result::get(const StringType&, const double&) const;
template StringType Result::get(const StringType&, const StringType&) const;
template date Result::get(const StringType&, const date&) const;
template time Result::get(const StringType&, const time&) const;
template timestamp Result::get(const StringType&, const timestamp&) const;
template std::vector<std::uint8_t>
Result::get(const StringType&, const std::vector<std::uint8_t>&) const;

} // namespace Nanodbc

#undef NANODBC_THROW_DATABASE_ERROR
#undef NANODBC_STRINGIZE
#undef NANODBC_STRINGIZE_I
#undef NANODBC_CALL_RC
#undef NANODBC_CALL

#endif // DOXYGEN
