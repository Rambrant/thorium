#include "objc.hpp"

#include <cctype>

namespace launcher::objc
{
    auto normaliseEncoding( const char * runtimeEncoding) -> std::string
    {
        std::string  result;

        for ( const char * c = runtimeEncoding; *c != '\0'; ++c)
        {
            if ( std::isdigit( static_cast<unsigned char>( *c)))
            {
                continue;
            }
            if ( *c == 'r' && ( c[ 1] == '*' || c[ 1] == '^'))
            {
                continue;
            }
            result += *c;
        }
        return result;
    }

    auto selector( const char * name) -> SEL
    {
        return sel_registerName( name);
    }
}
