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
        return std::cerr << "error: connection to database failed" << std::endl, -1;

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
