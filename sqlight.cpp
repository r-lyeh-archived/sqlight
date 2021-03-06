// SQLight, tiny MySQL C++11 client. Based on code by Ladislav Nevery.
// - rlyeh, 2013. zlib/libpng licensed

#include <string.h>

#include <cstdint>
#include <cassert>

#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)

#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <windows.h>
#   ifdef min
#   undef min
#   endif
#   ifdef max
#   undef max
#   endif

#   pragma comment(lib,"ws2_32.lib")

#   define INIT()                   { static WSADATA wsa; WSAStartup(MAKEWORD(1,1),&wsa); }

#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::closesocket((A))
#   define READ(A,B,C)               ::read((A),(B),(C))
#   define RECV(A,B,C,D)             ::recv((A), (char *)(B), (C), (D))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const char *)(B), (int)(C), (D))
#   define WRITE(A,B,C)              ::write((A),(B),(C))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((A),(B),(C),(char *)(D), (int*)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((A),(B),(C),(char *)(D), (int )(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),2)
#   define SHUTDOWN_R(A)             ::shutdown((A),0)
#   define SHUTDOWN_W(A)             ::shutdown((A),1)

    namespace
    {
        // fill missing api

        enum
        {
            F_GETFL = 0,
            F_SETFL = 1,

            O_NONBLOCK = 128 // dummy
        };

        int fcntl( int &sockfd, int mode, int value )
        {
            if( mode == F_GETFL ) // get socket status flags
                return 0; // original return current sockfd flags

            if( mode == F_SETFL ) // set socket status flags
            {
                u_long iMode = ( value & O_NONBLOCK ? 0 : 1 );

                bool result = ( ioctlsocket( sockfd, FIONBIO, &iMode ) == NO_ERROR );

                return 0;
            }

            return 0;
        }
    }

#   define $windows(...) __VA_ARGS__
#   define $welse(...)

#else

#   include <fcntl.h>
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <netdb.h>
#   include <unistd.h>    //close

#   include <arpa/inet.h> //inet_addr

#   define INIT()                    {}

#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::close((A))
#   define READ(A,B,C)               ::read((A),(B),(C))
#   define RECV(A,B,C,D)             ::recv((A), (void *)(B), (C), (D))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const std::int8_t *)(B), (C), (D))
#   define WRITE(A,B,C)              ::write((A),(B),(C))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((int)(A),(int)(B),(int)(C),(      void *)(D),(socklen_t *)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((int)(A),(int)(B),(int)(C),(const void *)(D),(int)(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),SHUT_RDWR)
#   define SHUTDOWN_R(A)             ::shutdown((A),SHUT_RD)
#   define SHUTDOWN_W(A)             ::shutdown((A),SHUT_WR)

#   define $welse(...) __VA_ARGS__
#   define $windows(...)

#endif

#include "sqlight.hpp"

namespace {
        // knot c&p in the future we must do it right
        enum
        {
            TCP_OK = 0,
            TCP_ERROR = -1,
            TCP_TIMEOUT = -2
    //      TCP_DISCONNECTED = -3,
        };

        timeval as_timeval( double seconds )
        {
            timeval tv;
            tv.tv_sec = (int)(seconds);
            tv.tv_usec = (int)((seconds - (int)(seconds)) * 1000000.0);
            return tv;
        }

        int select( int &sockfd, double timeout )
        {
            // set up the file descriptor set
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            // set up the struct timeval for the timeout
            timeval tv = as_timeval( timeout );

            // wait until timeout or data received
            // if  tv = {n,m}, then select() waits up to n.m seconds
            // if  tv = {0,0}, then select() does polling
            // if &tv =  NULL, then select() waits forever
            int ret = SELECT(sockfd+1, &fds, NULL, NULL, &tv);
            return ( ret == -1 ? sockfd = -1, TCP_ERROR : ret == 0 ? TCP_TIMEOUT : TCP_OK );
        }

    // We must complete socket functions, timeout socket receptions.
    // This is just a fix and it will block if count bytes are
    // never received
    int recvfixed(int sockfd, void *buffer, size_t count, int flags) {
        char *_buffer = (char*)buffer;
        while (count) {
            if (select(sockfd, 1)!=TCP_OK)
                return -1;
            int total = RECV(sockfd, _buffer, count, flags);
            // Sometimes when the socket is closed it has unexpected behavior
            if (total<=0)
                return -1;

            _buffer+=total;
            count-=total;
        }
        return 1;
    }

    std::deque< std::string > tokenize( const std::string &input, const std::string &delimiters ) {
        std::string map( 256, '\0' );
        for( auto &ch : delimiters )
            map[ ch ] = '\1';
        std::deque< std::string > tokens(1);
    // for( auto &ch : input)  doesn't behave well here
        for( const unsigned char &ch : input ) {
            /**/ if( !map.at(ch)          ) tokens.back().push_back( ch );
            else if( tokens.back().size() ) tokens.push_back( std::string() );
        }
        while( tokens.size() && !tokens.back().size() ) tokens.pop_back();
        return tokens;
    }

    typedef uint32_t basetype;
    typedef std::vector<basetype> hash;

    // Mostly based on Paul E. Jones' sha1 implementation
    static hash SHA1( const void *pMem, basetype iLen, hash &my_hash )
    {
        // if( pMem == 0 || iLen == 0 ) return my_hash;

        struct Process
        {
            static basetype CircularShift(int bits, basetype word)
            {
                return ((word << bits) & 0xFFFFFFFF) | ((word & 0xFFFFFFFF) >> (32-bits));
            }

            static void MessageBlock( hash &H, unsigned char *Message_Block, int &Message_Block_Index )
            {
                const basetype K[] = {                  // Constants defined for SHA-1
                    0x5A827999,
                    0x6ED9EBA1,
                    0x8F1BBCDC,
                    0xCA62C1D6
                    };
                int     t;                          // Loop counter
                basetype    temp;                       // Temporary word value
                basetype    W[80];                      // Word sequence
                basetype    A, B, C, D, E;              // Word buffers

                /*
                 *  Initialize the first 16 words in the array W
                 */
                for(t = 0; t < 16; t++)
                {
                    W[t] = ((basetype) Message_Block[t * 4]) << 24;
                    W[t] |= ((basetype) Message_Block[t * 4 + 1]) << 16;
                    W[t] |= ((basetype) Message_Block[t * 4 + 2]) << 8;
                    W[t] |= ((basetype) Message_Block[t * 4 + 3]);
                }

                for(t = 16; t < 80; t++)
                {
                    W[t] = CircularShift(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
                }

                A = H[0];
                B = H[1];
                C = H[2];
                D = H[3];
                E = H[4];

                for(t = 0; t < 20; t++)
                {
                    temp = CircularShift(5,A) + ((B & C) | ((~B) & D)) + E + W[t] + K[0]; //D^(B&(C^D))
                    temp &= 0xFFFFFFFF;
                    E = D;
                    D = C;
                    C = CircularShift(30,B);
                    B = A;
                    A = temp;
                }

                for(t = 20; t < 40; t++)
                {
                    temp = CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
                    temp &= 0xFFFFFFFF;
                    E = D;
                    D = C;
                    C = CircularShift(30,B);
                    B = A;
                    A = temp;
                }

                for(t = 40; t < 60; t++)
                {
                    temp = CircularShift(5,A) +
                    ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];         //(B & C) | (D & (B | C))
                    temp &= 0xFFFFFFFF;
                    E = D;
                    D = C;
                    C = CircularShift(30,B);
                    B = A;
                    A = temp;
                }

                for(t = 60; t < 80; t++)
                {
                    temp = CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
                    temp &= 0xFFFFFFFF;
                    E = D;
                    D = C;
                    C = CircularShift(30,B);
                    B = A;
                    A = temp;
                }

                H[0] = (H[0] + A) & 0xFFFFFFFF;
                H[1] = (H[1] + B) & 0xFFFFFFFF;
                H[2] = (H[2] + C) & 0xFFFFFFFF;
                H[3] = (H[3] + D) & 0xFFFFFFFF;
                H[4] = (H[4] + E) & 0xFFFFFFFF;

                Message_Block_Index = 0;
            }
        };

        // 512-bit message blocks
        unsigned char Message_Block[64];
        // Index into message block array
        int Message_Block_Index = 0;
        // Message length in bits
        basetype Length_Low = 0, Length_High = 0;

        if( iLen > 0 )
        {
            // Is the message digest corrupted?
            bool Corrupted = false;

            // Input()

            unsigned char *message_array = (unsigned char *)pMem;

            while(iLen-- && !Corrupted)
            {
                Message_Block[Message_Block_Index++] = (*message_array & 0xFF);

                Length_Low += 8;
                Length_Low &= 0xFFFFFFFF;               // Force it to 32 bits
                if (Length_Low == 0)
                {
                    Length_High++;
                    Length_High &= 0xFFFFFFFF;          // Force it to 32 bits
                    if (Length_High == 0)
                    {
                        Corrupted = true;               // Message is too long
                    }
                }

                if (Message_Block_Index == 64)
                {
                    Process::MessageBlock( my_hash, Message_Block, Message_Block_Index );
                }

                message_array++;
            }

            assert( !Corrupted );
        }

        // Result() and PadMessage()

        /*
        *  Check to see if the current message block is too small to hold
        *  the initial padding bits and length.  If so, we will pad the
        *  block, process it, and then continue padding into a second block.
        */
        if (Message_Block_Index > 55)
        {
            Message_Block[Message_Block_Index++] = 0x80;

            while(Message_Block_Index < 64)
            {
                Message_Block[Message_Block_Index++] = 0;
            }

            Process::MessageBlock( my_hash, Message_Block, Message_Block_Index );

            while(Message_Block_Index < 56)
            {
                Message_Block[Message_Block_Index++] = 0;
            }
        }
        else
        {
            Message_Block[Message_Block_Index++] = 0x80;

            while(Message_Block_Index < 56)
            {
                Message_Block[Message_Block_Index++] = 0;
            }
        }

        /*
         *  Store the message length as the last 8 octets
         */
        Message_Block[56] = (Length_High >> 24) & 0xFF;
        Message_Block[57] = (Length_High >> 16) & 0xFF;
        Message_Block[58] = (Length_High >> 8) & 0xFF;
        Message_Block[59] = (Length_High) & 0xFF;
        Message_Block[60] = (Length_Low >> 24) & 0xFF;
        Message_Block[61] = (Length_Low >> 16) & 0xFF;
        Message_Block[62] = (Length_Low >> 8) & 0xFF;
        Message_Block[63] = (Length_Low) & 0xFF;

        Process::MessageBlock( my_hash, Message_Block, Message_Block_Index );

        return my_hash;
    }

    template< typename T >
    hash SHA1( const T &input ) {
        hash h(5);
        h[0] = 0x67452301, h[1] = 0xEFCDAB89,
        h[2] = 0x98BADCFE, h[3] = 0x10325476,
        h[4] = 0xC3D2E1F0;
        hash raw = SHA1( input.data(), input.size() * sizeof( *input.begin() ), h );
        return raw;
    }

    hash SHA1( const void *mem, int len ) {
        hash h(5);
        h[0] = 0x67452301, h[1] = 0xEFCDAB89,
        h[2] = 0x98BADCFE, h[3] = 0x10325476,
        h[4] = 0xC3D2E1F0;
        hash raw = SHA1( mem, len, h );
        return raw;
    }

    std::vector<unsigned char> blob( const hash &raw ) {
        std::vector<unsigned char> blob;
        for( std::vector<basetype>::const_iterator it = raw.begin(); it != raw.end(); ++it ) {
            static int i = 1;
            static char *low = (char*)&i;
            static bool is_big = ( *low ? false : true );
            if( is_big )
                for( int i = 0; i < sizeof(basetype); ++i )
                    blob.push_back( ( (*it) >> (i * 8) ) & 0xff );
            else
                for( int i = sizeof(basetype) - 1; i >= 0; --i )
                    blob.push_back( ( (*it) >> (i * 8) ) & 0xff );
        }
        return blob;
    }
}

namespace
{
    std::vector<unsigned char> get_mysql_hash( const std::vector<unsigned char> &password, const unsigned char salt[20] )
    {
        // [ref] http://dev.mysql.com/doc/internals/en/authentication-method.html#packet-Authentication::Native41
        // SHA1( password ) XOR SHA1( "20-bytes random data from server" <concat> SHA1( SHA1( password ) ) )

        std::vector<unsigned char> hash1 = password; // blob( SHA1( "password" ) );
        std::vector<unsigned char> hash2 = blob( SHA1( hash1 ) );

        unsigned char concat[40];
        for( size_t i = 0; i < 40; i++ )
            concat[i] = ( i < 20 ? salt[i] : hash2[i-20] );

        std::vector<unsigned char> hash3 = blob( SHA1( concat, 40 ) );

        for( size_t i = 0; i < 20; ++i )
            hash3[i] ^= hash1[i];

        return hash3;
    }
}

#ifdef _MSC_VER
#   pragma warning( push )
#   pragma warning( disable : 4996 )
#endif

sq::light::light() : s(0), connected(false) {
    INIT();
}

sq::light::~light() {
    disconnect();
}

bool sq::light::acquire() {
    release();

    buf.resize( 1 << 24 ); // recv buffer is max row size 16 mb
    b = d = buf.data();

    return true;
}

void sq::light::release() {
    buf = std::vector<char>();
    b = d = 0;
}

bool sq::light::connect( const std::string &host, const std::string &port, const std::string &user, const std::string &pass )
{
    unsigned _port;

    if( !(std::stringstream(port) >> _port) )
        return false;

    if( user.empty() || pass.empty() || host.empty() || _port == 0 || _port >= 65536 )
        return false;

    this->host = host;
    this->port = port;
    this->user = user;
    this->pass = blob( SHA1( pass ) );

    return reconnect();
}

bool sq::light::reconnect() {
    disconnect();
    release();

    if( !acquire() )
        return false;

    connected = true;
    if( !test("SHOW DATABASES") )
        return connected = false;

    return connected = true;
}

void sq::light::disconnect() {
    if( s ) CLOSE( s );
    s = 0;
    connected = false;
}

bool sq::light::is_connected() {
  char buf;

  if (!connected)
    return false;

  int res = recvfixed(s, &buf, 1, MSG_PEEK | $windows(0) $welse(MSG_DONTWAIT));
  if (res<0)
    {
      // Maybe disconnected or maybe not...
      if (errno != EAGAIN && errno != EWOULDBLOCK)
    connected = false;
    }
  else if (res==0)
    {
      connected = false;
    }

  return connected;
}

bool sq::light::fail( const char *error, const char *title )
{
    //disconnect();
    return false;
}

bool sq::light::open()
{
    if(!s) {

        unsigned _port;
        if( !(std::stringstream(port) >> _port) )
            return fail("invalid port");

        sockaddr_in addr;
        memset(&addr,0,sizeof(sockaddr_in));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(_port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

        if( CONNECT(s,(sockaddr*)&addr,sizeof(addr)) <  0 )
            return fail("Connect Failed  ");

        i = RECV(s,b,1<<24,0);
        if (b[4] < 10 ) return fail(b+5,"Need MySql > 4.1");

        // Read server auth challenge and calc response by making SHA1 hashes from it and password
        // [ref] http://dev.mysql.com/doc/internals/en/connection-phase.html#packet-Protocol::Handshake

        std::vector<unsigned char> hash;

        {
            byte *salt = (byte*)b+strlen(b+5)+10;
            memcpy(salt+8,salt+27,13);
            hash = get_mysql_hash( pass, salt );
        }

        // Construct client auth response
        // [ref] http://forge.mysql.com/wiki/MySQL_Internals_ClientServer_Protocol#Client_Authentication_Packet

            d = b+4;

          *(int*)d =
            CLIENT_FOUND_ROWS|
            CLIENT_PROTOCOL_41|
            CLIENT_SECURE_CONNECTION|
            CLIENT_LONG_PASSWORD|
            CLIENT_MULTI_RESULTS;        // for stored procedures
                       d+=4;

          *(int*)d = 1<<24;             d+=4;      // max packet size = 16Mb
               * d = 8;                 d+=24;     // utf8 charset
          strcpy(d,user.c_str());       d+=1 + user.size();
               * d = 20;                d+=1;  for(i=0;i<20;i++)
              d[i] = hash[i]^0;         d+=22;     // XOR encrypt response
          *(int*)b = d-b-4 | 1<<24;                // calc final packet size and id

          SEND(s,b,  d-b,0);

          RECV(s,(char*)&no,4,0); no&=(1<<24)-1;   // in case of login failure server sends us an error text
        i=RECV(s,b,no,0);        if(i==-1||*b)     return fail(i==-1?"Timeout":b+3,"Login Failed");
    }

    return true;
}

bool sq::light::sends( const std::string &query )
{
    // Send sql query
    // Details at: http://forge.mysql.com/wiki/MySQL_Internals_ClientServer_Protocol#Command_Packet

    d[4]=0x3;
    strcpy(d+5,(char*)query.c_str());
    *(int*)d=strlen(d+5)+1;
    i=SEND(s,d,4+*(int*)d, $windows(0) $welse(MSG_NOSIGNAL));
    if (i<0)
        return false;
    return true;
}

bool sq::light::recvs( void *userdata, void* onvalue, void* onfield, void *onsep )
{
    // Parse and display record set
    // Details at: http://forge.mysql.com/wiki/MySQL_Internals_ClientServer_Protocol#Result_Set_Header_Packet
    // [ref] http://dev.mysql.com/doc/internals/en/overview.html#status-flags

    std::vector<char> txtv(65536, 0); // medium blob
    char *txt = txtv.data();

    char *p=b; byte typ[1000]={0}; int fields=0, field=0, value=0, row=0, exit=0, rc=0;

    char &b0 = b[0];
    char &b1 = b[1]; // warning count  low byte
    char &b2 = b[2]; // warning count high byte
    char &b3 = b[3]; // status  low byte
    char &b4 = b[4]; // status high byte

    while (1) {
               rc = 0;
               i= recvfixed(s, (char*)&no, 4, 0);
               // i=RECV(s,(char*)&no,4,0);
               no&=0xffffff; // This is a bug. server sometimes don't send those 4 bytes together. will fix it (recvfixed fix the bug)
               // this also helps to skip packet sequence number: http://mysql.timesoft.cc/doc/internals/en/the-packet-header.html
               if (i<0)
                   return fail("connection lost");

        while( rc < no && (i=RECV(s,b+rc, no-rc ,0)) > 0  ) rc+=i;

        if(i<1) return fail("connection lost"); // Connection lost

        // 0. For non query sql commands we get just single success or failure response
        if(*       b==0x00&&!exit)                                      break;   // success
        if(*(byte*)b==0xff&&!exit)  { b[*(short*)(b+1)+3]=0; return fail(b+3); } // failure: show server error text

        // 1. first thing we receive is number of fields
        if(!fields ) { memcpy(&fields,b,no); field=fields; continue; }

        // 3. 5. after receiving last field info or row we get this EOF marker or more results exist
        if (*(byte*)b==0xfe && no < 9)
        {
            if( no == 5 )
            {
                int status = ( b4 << 8 ) | b3;
                if( status & 0x0008 ) { // SERVER_MORE_RESULTS_EXISTS
                    typedef void (*TOnSep)(void *);  if(onsep) ((TOnSep)onsep)(userdata);
                    continue;
                }
            }

            if(exit++) break; else continue;        // EOF
        }

        // 4. after receiving all field infos we receive row field values. One row per Receive/Packet
        while( value  ) {
            *txt=0; i=fields-value; size_t len=1; byte g=*(byte*)p;

            // ~net_field_length() @ libmysql.c {
                switch(g) {
                    case 0:
                    case 251: g=0;      break; // NULL_LENGTH
                    default:
                    case   1: g=1;      break;
                    case 252: g=2, ++p; break;
                    case 253: g=3, ++p; break;
                    case 254: g=8, ++p; break;
                }
                // @todo: beware little/big endianess here!
                memcpy(&len,p,g); p+=g;
            //}

            auto &type = typ[i];
            switch( type )
            {
                case FIELD_TYPE_BIT:
                case FIELD_TYPE_BLOB:
                case FIELD_TYPE_DATE:
                case FIELD_TYPE_DATETIME:
                case FIELD_TYPE_DECIMAL:
                case FIELD_TYPE_DOUBLE:
                case FIELD_TYPE_ENUM:
                case FIELD_TYPE_FLOAT:
                case FIELD_TYPE_GEOMETRY:
                case FIELD_TYPE_INT24:
                case FIELD_TYPE_LONG:
                case FIELD_TYPE_LONG_BLOB:
                case FIELD_TYPE_LONGLONG:
                case FIELD_TYPE_MEDIUM_BLOB:
                case FIELD_TYPE_NEW_DECIMAL:
                case FIELD_TYPE_NEWDATE:
                case FIELD_TYPE_NULL:
                case FIELD_TYPE_SET:
                case FIELD_TYPE_SHORT:
                case FIELD_TYPE_STRING:
                case FIELD_TYPE_TIME:
                case FIELD_TYPE_TIMESTAMP:
                case FIELD_TYPE_TINY:
                case FIELD_TYPE_TINY_BLOB:
                case FIELD_TYPE_VAR_STRING:
                case FIELD_TYPE_VARCHAR:
                case FIELD_TYPE_YEAR:
                default: {
                    // @todo: beware little/big endianess here!
                    if(g) memcpy(txt,p,len); txt[len]=0;
                    typedef long (*TOnValue)(void *,char*,int,int,int);  if(onvalue) ret=((TOnValue)onvalue)(userdata,txt,row,i,type);
                    break;
                }
            }

            p+=len;
            if(!--value) { row++; value=fields; p=b; break; }
        }

        // 2. Second info we get are field infos like name type etc. One field per Receive/Packet
        if( field  ) {
                  i        = fields - field;
            char* cat      =          p; p+=1+*p; *   cat ++=0;
            char* db       =          p; p+=1+*p; *    db ++=0;
            char* table    =          p; p+=1+*p; * table ++=0;
            char* table_   =          p; p+=1+*p; * table_++=0;
            char* name     =          p; p+=1+*p; *  name ++=0;
            char* name_    =          p; p+=1+*p; *  name_++=0; *p++=0;
            short charset  = *(short*)p; p+=2;
            long  length   = * (long*)p; p+=4;
                  typ[i]   = * (byte*)p; p+=1;
            short flags    = *(short*)p; p+=2;
            byte  digits   = * (byte*)p; p+=3;
            char* Default  =          p;

            auto &type = typ[i];
            switch( type )
            {
                case FIELD_TYPE_STRING:
                case FIELD_TYPE_VAR_STRING:
                case FIELD_TYPE_LONG:
                case FIELD_TYPE_LONGLONG:
                case FIELD_TYPE_DATETIME:
                case FIELD_TYPE_DATE:
                case FIELD_TYPE_FLOAT:
                case FIELD_TYPE_TINY:
                case FIELD_TYPE_BLOB:
                case FIELD_TYPE_NEW_DECIMAL:
                    break;
                default:
                std::cerr << "Warning: unsupported type " << int(type) << " in column: " << name << std::endl;
            }

            if(!--field) value = fields; p=b; length=std::max(length*3,60L); length=std::min(length,200L);
            typedef long (*TOnField)(void *,char*,int,int,int);  if(onfield) ((TOnField)onfield)(userdata,name,row,i,length);
        }
    }

    return true;
}

namespace
{
    struct local {
        int x, y;
        std::vector< char * > data;
        std::vector< char * > head;

        local() : x(0),y(0) {
        }

        ~local() {
            for( unsigned i = 0, end = data.size(); i < end; ++i )
                if( data[i] ) free( data[i] ), data[i] = 0;
            for( unsigned i = 0, end = head.size(); i < end; ++i )
                if( head[i] ) free( head[i] ), head[i] = 0;
        }
    };

    void GetText2f( void *userdata, char* txt ) {
        local *l = (local *)userdata;
        l->head.push_back( txt ? strdup(txt) : strdup("") );
    }
    void GetText2v( void *userdata, char* txt ) {
        local *l = (local *)userdata;
        l->data.push_back( txt ? strdup(txt) : strdup("") );
    }

    void GetText3f( void *userdata, char* txt ) {
        local *l = (local *)userdata;
        l->x++;
        l->y++;
        l->data.push_back( txt ? strdup(txt) : strdup("") );
    }
    void GetText3v( void *userdata, char* txt ) {
        local *l = (local *)userdata;
        l->y++;
        l->data.push_back( txt ? strdup(txt) : strdup("") );
    }
}

bool sq::light::test( const std::string &query )
{
    if( !connected )
        return false;

    std::lock_guard<std::mutex> lock(mutex);

    no = 20;
    ret = 0;

    if( !query.empty() )
        if( open() ) // setup
            if( sends(query) ) // send
                if( recvs(0, 0, 0, 0) ) // recv and parse
                    return true;

    return false;
}

bool sq::light::exec( const std::string &query, sq::light::callback3 cb3, void *userdata )
{
    if( !connected )
        return false;

    auto create_index = []( const std::string &sqlcode ) {
        auto tokens = tokenize(sqlcode," (");
        return tokens.size() > 1 ? tokens.at(1) : std::string();
    };

    std::lock_guard<std::mutex> lock(mutex);

    sq::metrics metrics(create_index(query));

        local l;

        no = 20;
        ret = 0;

        if( !query.empty() )
            if( open() ) // setup
                if( sends(query) ) // send
                    if( recvs( (void *)&l /*userdata*/, (void *)GetText3v, (void *)GetText3f, 0 /*onsep*/) ) { // recv and parse
                        if( l.x > 0 )
                            (*cb3)( userdata, l.x, l.y / l.x, (const char **)l.data.data() );
                        return true;
                    }

    metrics.cancel();
    return false;
}

namespace {
    std::string escape( const std::string &text )  {
        std::string out;
        for( auto &it : text ) {
            /**/ if( it == '\\' ) out += "\\\\";
            else if( it == '\r' ) out += "\\r";
            else if( it == '\n' ) out += "\\n";
            else if( it == '\t' ) out += "\\t";
            else if( it == '\f' ) out += "\\f";
            else if( it == '\b' ) out += "\\b";
            else if( it == '\v' ) out += "\\v";
            else if( it ==  '"' ) out += "\\\"";
            else                  out += it;
        }
        return out;
    }
    void OnJSONCb( void *userdata, int w, int h, const char **map ) {
        std::string &array = *((std::string*)userdata);
        const char **field = &map[0];
        const char **value = &map[w];
        for( int y = 1; y < h; ++y ) {
            array += "{\n";
            for( int x = 0; x < w; ++x ) {
                array += "\"" + escape(std::string(*field++)) + "\": ";
                array += "\"" + escape(std::string(*value++)) + "\",\n";
            }
            if( w > 0 ) array[array.size()-2] = ' ';
            array += "},\n";

            field = map;
        }
        if( h > 0 && array.size() >= 2 ) array[array.size()-2] = ' ';
        array = "[\n" + array + "]\n";
    }
}

bool sq::light::json( const std::string &query, std::string &result ) {
    result = std::string();
    bool ok = exec(query,OnJSONCb,(void *)&result);
    if( !ok )
        result = std::string();
    return ok;
}

std::string sq::light::json( const std::string &query ) {
    std::string result;
    return json(query,result) ? result : std::string();
}

namespace {

    struct stats
    {
        stats()
        {}

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

        std::vector<std::string> report( const std::string &_fmt123456, const std::string &sort_key, bool reversed ) const {

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

        mutable std::map< std::string /*call*/, std::vector<double> /*hits*/ > map;
    } allstats;
}


sq::metrics::metrics( const std::string &index )
    : cancelled(false), then(std::chrono::steady_clock::now()), idx(index) {
}

sq::metrics::~metrics() {
    done();
}

void sq::metrics::done() {
    if( !cancelled ) {
        using namespace std::chrono;
        double taken = duration_cast<duration<double,std::ratio<1>>>(steady_clock::now() - then).count();
        allstats.hit(idx, taken);
    }
    cancel();
}

void sq::metrics::cancel() {
    cancelled = true;
}

std::vector<std::string> sq::metrics::report( const std::string &_fmt123456, const std::string &sort_key, bool reversed ) {
    return allstats.report( _fmt123456, sort_key, reversed );
}

#undef $welse
#undef $windows

#undef INIT

#undef ACCEPT
#undef CONNECT
#undef CLOSE
#undef READ
#undef RECV
#undef SELECT
#undef SEND
#undef WRITE
#undef GETSOCKOPT
#undef SETSOCKOPT

#undef BIND
#undef LISTEN
#undef SHUTDOWN
#undef SHUTDOWN_R
#undef SHUTDOWN_W

#ifdef _MSC_VER
#   pragma warning( pop )
#endif
