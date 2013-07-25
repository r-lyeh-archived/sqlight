#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <string>
#include <vector>

//#include "wire/wire.hpp"

namespace sq
{
    struct stats
    {
        mutable std::map< std::string /*call*/, std::vector<double> /*hits*/ > map;

        stats()
        {}

        public:

        void hit( const std::string &idx, double taken ) {
            map[idx].push_back( taken );
        }

        std::vector<std::string> list() const {
            std::vector<std::string> vec;
            for( auto &it : map )
                vec.push_back( it.first );
            return vec;
        }

        double mini( const std::string &idx ) const {
            const auto &vec = map[idx];
            return *std::min_element(vec.begin(), vec.end());
        }

        double maxi( const std::string &idx ) const {
            const auto &vec = map[idx];
            return *std::max_element(vec.begin(), vec.end());
        }

        double total( const std::string &idx, double div = 1 ) const {
            long double t = 0;
            for( auto &in : map[idx] )
                t += in;
            return t / div;
        }

        double avg( const std::string &idx ) const {
            return total(idx, hits(idx) ? hits(idx) : 1 );
        }

        size_t hits( const std::string &idx ) const {
            return map[idx].size();
        }

#if 0
        // using wire::

        std::vector<std::string> report( const std::string &_fmt123456, const std::string &sort_key = "{total}", bool reversed = true ) const {

            wire::string fmt123456 = wire::string(_fmt123456)
                .replace(  "{idx}", "\1")
                .replace(  "{min}", "\2")
                .replace(  "{max}", "\3")
                .replace("{total}", "\4")
                .replace(  "{avg}", "\5")
                .replace( "{hits}", "\6");

            std::string sort_by;
            /**/ if( sort_key ==   "{idx}" ) sort_by = "\1";
            else if( sort_key ==   "{min}" ) sort_by = "\2";
            else if( sort_key ==   "{max}" ) sort_by = "\3";
            else if( sort_key == "{total}" ) sort_by = "\4";
            else if( sort_key ==   "{avg}" ) sort_by = "\5";
            else if( sort_key ==  "{hits}" ) sort_by = "\6";
            else                             sort_by = "\1";

            std::map<double,std::vector<std::string>> sort;
            if( sort_by == "\1" ) {
                for( auto &m : map ) {
                    const std::string &idx = m.first;
                    wire::string fmt( fmt123456, idx, mini(idx),maxi(idx),total(idx),avg(idx),hits(idx) );
                    sort[ sort.size() ].push_back( fmt );
                }
            } else {
                for( auto &m : map ) {
                    const std::string &idx = m.first;
                    wire::string res( sort_by, idx, mini(idx),maxi(idx),total(idx),avg(idx),hits(idx) );
                    wire::string fmt( fmt123456, idx, mini(idx),maxi(idx),total(idx),avg(idx),hits(idx) );
                    sort[ res.as<double>() ].push_back( fmt );
                }
            }

            std::vector<std::string> out;
            if( reversed ) {
                for( auto m = sort.rbegin(); m != sort.rend(); ++m ) {
                    for( auto &i : m->second ) {
                        out.push_back( i );
                    }
                }
            }
            else {
                for( auto &m : sort ) {
                    for( auto &i : m.second ) {
                        out.push_back( i );
                    }
                }
            }
            return out;
        }

#else

       std::vector<std::string> report( const std::string &_fmt123456, const std::string &sort_key = "{total}", bool reversed = true ) const {

            auto format = []( const std::string &fmt123456, const std::string &a, double b, double c, double d, double e, size_t f ){
                std::stringstream ss;
                for( auto &ch : fmt123456 ) {
                    /**/ if( ch == '\1' ) ss << a;
                    else if( ch == '\2' ) ss << b;
                    else if( ch == '\3' ) ss << c;
                    else if( ch == '\4' ) ss << d;
                    else if( ch == '\5' ) ss << e;
                    else if( ch == '\6' ) ss << f;
                    else                  ss << ch;
                }
                return ss.str();
            };

            auto replace = []( const std::string &text, const std::string &from, const std::string &to ) {
                size_t found = 0;
                std::string s = text;
                while( ( found = s.find( from, found ) ) != std::string::npos ) {
                    s.replace( found, from.length(), to );
                    found += to.length();
                }
                return s;
            };

            std::string fmt123456 = _fmt123456;
            fmt123456 = replace( fmt123456,   "{idx}", "\1" );
            fmt123456 = replace( fmt123456,   "{min}", "\2" );
            fmt123456 = replace( fmt123456,   "{max}", "\3" );
            fmt123456 = replace( fmt123456, "{total}", "\4" );
            fmt123456 = replace( fmt123456,   "{avg}", "\5" );
            fmt123456 = replace( fmt123456,  "{hits}", "\6" );

            std::string sort_by;
            /**/ if( sort_key ==   "{idx}" ) sort_by = "\1";
            else if( sort_key ==   "{min}" ) sort_by = "\2";
            else if( sort_key ==   "{max}" ) sort_by = "\3";
            else if( sort_key == "{total}" ) sort_by = "\4";
            else if( sort_key ==   "{avg}" ) sort_by = "\5";
            else if( sort_key ==  "{hits}" ) sort_by = "\6";
            else                             sort_by = "\1";

            std::map<double,std::vector<std::string>> sort;
            if( sort_by == "\1" ) {
                for( auto &m : map ) {
                    const std::string &idx = m.first;
                    std::string fmt = format( fmt123456, idx, mini(idx),maxi(idx),total(idx),avg(idx),hits(idx) );
                    sort[ sort.size() ].push_back( fmt );
                }
            } else {
                for( auto &m : map ) {
                    const std::string &idx = m.first;
                    std::string res = format( sort_by, idx, mini(idx),maxi(idx),total(idx),avg(idx),hits(idx) );
                    std::string fmt = format( fmt123456, idx, mini(idx),maxi(idx),total(idx),avg(idx),hits(idx) );

                    double as_double;
                    std::stringstream ss;
                    ss << res, ss >> as_double;
                    sort[ as_double ].push_back( fmt );
                }
            }

            std::vector<std::string> out;
            if( reversed ) {
                for( auto m = sort.rbegin(); m != sort.rend(); ++m ) {
                    for( auto &i : m->second ) {
                        out.push_back( i );
                    }
                }
            } else {
                for( auto &m : sort ) {
                    for( auto &i : m.second ) {
                        out.push_back( i );
                    }
                }
            }
            return out;
        }

#endif

        static stats& get() {
            static stats sql;
            return sql;
        }
    };

    class metrics
    {
        metrics();
        metrics( const metrics &other );
        metrics &operator=( const metrics &other );

        bool cancelled;
        std::string idx;
        std::chrono::steady_clock::time_point then;

    public:

        explicit
        metrics( const std::string &index )
            : cancelled(false), then(std::chrono::steady_clock::now()), idx(index) {
        }

        ~metrics() {
            done();
        }

        void done() {
            if( !cancelled ) {
                using namespace std::chrono;
                double taken = duration_cast<duration<double,std::ratio<1>>>(steady_clock::now() - then).count();
                stats::get().hit(idx, taken);
            }
            cancel();
        }

        void cancel() {
            cancelled = true;
        }

        static std::vector<std::string> report( const std::string &_fmt123456, const std::string &sort_key = "{total}", bool reversed = true ) {
            return stats::get().report( _fmt123456, sort_key, reversed );
        }
    };

}
