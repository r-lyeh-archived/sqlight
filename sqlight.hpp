// SQLight, tiny MySQL C++11 client. Based on code by Ladislav Nevery.
// - rlyeh, 2013. zlib/libpng licensed

#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define SQLIGHT_VERSION "1.0.0" // (2015/09/10) Initial semantic versioning adherence

namespace sq
{
    class light
    {
    public:
        enum : unsigned {
            CLIENT_LONG_PASSWORD = 1,           /* New more secure passwords */
            CLIENT_FOUND_ROWS = 2,              /* Found instead of affected rows */
            CLIENT_LONG_FLAG = 4,               /* Get all column flags */
            CLIENT_CONNECT_WITH_DB = 8,         /* One can specify db on connect */
            CLIENT_NO_SCHEMA = 16,              /* Don't allow database.table.column */
            CLIENT_COMPRESS = 32,               /* Can use compression protocol */
            CLIENT_ODBC = 64,                   /* Odbc client */
            CLIENT_LOCAL_FILES = 128,           /* Can use LOAD DATA LOCAL */
            CLIENT_IGNORE_SPACE = 256,          /* Ignore spaces before '(' */
            CLIENT_PROTOCOL_41 = 512,           /* New 4.1 protocol */
            CLIENT_INTERACTIVE = 1024,          /* This is an interactive client */
            CLIENT_SSL = 2048,                  /* Switch to SSL after handshake */
            CLIENT_IGNORE_SIGPIPE = 4096,       /* IGNORE sigpipes */
            CLIENT_TRANSACTIONS = 8192,         /* Client knows about transactions */
            CLIENT_RESERVED = 16384,            /* Old flag for 4.1 protocol */
            CLIENT_SECURE_CONNECTION = 32768,   /* New 4.1 authentication */
            CLIENT_MULTI_STATEMENTS = 65536,    /* Enable/disable multi-stmt support */
            CLIENT_MULTI_RESULTS = 131072       /* Enable/disable multi-results */
        //  CLIENT_REMEMBER_OPTIONS = (((ulong) 1) << 31)
        };

        enum : unsigned char {
            FIELD_TYPE_BIT = 16,
            FIELD_TYPE_BLOB = 252,
            FIELD_TYPE_DATE = 10,
            FIELD_TYPE_DATETIME = 12,
            FIELD_TYPE_DECIMAL = 0,
            FIELD_TYPE_DOUBLE = 5,
            FIELD_TYPE_ENUM = 247,
            FIELD_TYPE_FLOAT = 4,
            FIELD_TYPE_GEOMETRY = 255,
            FIELD_TYPE_INT24 = 9,
            FIELD_TYPE_LONG = 3,
            FIELD_TYPE_LONG_BLOB = 251,
            FIELD_TYPE_LONGLONG = 8,
            FIELD_TYPE_MEDIUM_BLOB = 250,
            FIELD_TYPE_NEW_DECIMAL = 246,
            FIELD_TYPE_NEWDATE = 14,
            FIELD_TYPE_NULL = 6,
            FIELD_TYPE_SET = 248,
            FIELD_TYPE_SHORT = 2,
            FIELD_TYPE_STRING = 254,
            FIELD_TYPE_TIME = 11,
            FIELD_TYPE_TIMESTAMP = 7,
            FIELD_TYPE_TINY = 1,
            FIELD_TYPE_TINY_BLOB = 249,
            FIELD_TYPE_VAR_STRING = 253,
            FIELD_TYPE_VARCHAR = 15,
            FIELD_TYPE_YEAR = 13
        };

        typedef unsigned char  byte;
        typedef unsigned short dword;

         light();
        ~light();

        bool connect( const std::string &host = "localhost", const std::string &port = "3306", const std::string &user = "root", const std::string &password = "root" );
        bool reconnect();
        void disconnect();
        bool is_connected();

        typedef void (*callback3) (void *userdata, int w, int h, const char **map );

        bool test( const std::string &query );
        bool exec( const std::string &query, sq::light::callback3 cb, void *userdata = (void*)0 );

        std::string json( const std::string &query );
        bool json( const std::string &query, std::string &result );

    protected:
        bool connected;
        std::string host, port, user;
        std::vector<unsigned char> pass;

        int s, i;
        unsigned ret, no;

        std::vector<char> buf;
        char *b, *d;

        std::mutex mutex;

        bool open();
        bool sends( const std::string &command );
        bool recvs( void *userdata, void* onvalue, void* onfield, void *onsep );
        bool fail( const char *error = 0, const char *title = 0 );
        bool acquire();
        void release();
    };

    class metrics
    {
    public:
        explicit metrics( const std::string &index );

        ~metrics();

        void done();
        void cancel();

        static std::vector<std::string> report( const std::string &_fmt123456, const std::string &sort_key = "{total}", bool reversed = true );

    protected:
        metrics();
        metrics( const metrics &other );
        metrics &operator=( const metrics &other );

        bool cancelled;
        std::string idx;
        std::chrono::steady_clock::time_point then;
    };
}
