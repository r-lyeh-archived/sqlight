SQLight <a href="https://travis-ci.org/r-lyeh/sqlight"><img src="https://api.travis-ci.org/r-lyeh/sqlight.svg?branch=master" align="right" /></a>
=======

- SQLight is a lightweight and simple MySQL client written in C++11.
- SQLight returns JSON documents. Document writing callback is overridable.
- SQLight supports multi-statement queries.
- SQLight has an optional transparent metrics interface.
- SQLight has no dependencies. Only standard headers are required.
- SQLight is cross-platform. Compiles under MSVC/GCC. Works on Windows/Linux.
- SQLight is tiny. One header and one source file.
- SQLight is based on code by Ladislav Nevery.
- SQLight is zlib/libpng licensed.

## Public API (sq::light)
- `.connect(host,port,user,pass)` connect to a MySQL database
- `.reconnect()` reconnect to database
- `.disconnect()` disconnect from database
- `.is_connected()` check if we are connected to database
- `.json(query)` get JSON document with data received from SQL query
- `.json(query,result)` get JSON document with data received from SQL query

## Public API (sq::light, optional)
- `.test(query)` check SQL query
- `.exec(query,callback,userdata)` call user-defined callback with data received from SQL query

## Public API (sq::metrics, optional)
- This is an optional metrics interface that could be dettached from SQLight. Check usage on `sqlight.cpp` file.

## Sample
```c++
#include <iostream>
#include <string>

#include "sqlight.hpp"

int main( int argc, const char **argv )
{
    // prompt user line

    auto prompt = []( const std::string &prompt ) {
        std::cout << prompt;
        std::string in;
        std::getline(std::cin, in);
        return in;
    };

    // mysql client

    sq::light sql;

    if( !sql.connect( argc > 1 ? argv[1] : "locahost", "3306", "root", prompt("password>") ) )
        return std::cerr << "error: connection to database failed" << std::endl, 0;

    std::cout << "connected to db. feed me SQL queries!" << std::endl;

    for( ;; ) {
        std::string input = prompt("sqlight>");

        if( input.empty() )
            continue;

        if( input == ".quit" || input == "quit" || input == "q" )
            break;

        std::string result;
        bool ok = sql.json(input, result);

        std::cout << result << std::endl;
        std::cout << ( ok ? "OK" : "ERROR" ) << std::endl;
    }

    // metrics report

    bool reversed = true;
    std::string format = "{idx} (x{hits}) total:{total} min:{min} max:{max} avg:{avg}";
    std::string sorted_by = "{total}";
    auto list = sq::metrics::report( format, sorted_by, reversed );
    for( auto &line : list )
        std::cout << line << std::endl;

    return 0;
}
```

## Possible output
```lisp
user@debian:~/sqlight$ ./sqlight.out
password>root
connected to db. feed me SQL queries!
sqlight>select count(*) from mysql.user
[
{
"count(*)": "7"
}
]

OK
sqlight>.quit
count (x1) total:0.004349 min:0.004349 max:0.004349 avg:0.004349
```

## Changelog
- v1.0.0 (2015/09/10)
  - Initial semantic versioning adherence

