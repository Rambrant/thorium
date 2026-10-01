#include "hal/topology/address_tables.hpp"

#include <algorithm>

namespace hal
{
    namespace detail
    {
        auto poolIn( const std::vector<PoolEntry> & entries, const std::string_view box) -> std::vector<Address>
        {
            std::vector<Address> candidates;

            for( const auto & entry : entries)
            {
                if( entry.Box == box)
                {
                    candidates.push_back( entry.Value);
                }
            }

            return candidates;
        }

        auto siteAddressIn(
            const std::vector<SiteEntry> & entries,
            const std::string_view         site,
            const std::string_view         box) -> std::optional<Address>
        {
            for( const auto & entry : entries)
            {
                if( entry.Site == site && entry.Box == box)
                {
                    return entry.Value;
                }
            }

            return std::nullopt;
        }

        auto siteNamesIn( const std::vector<SiteEntry> & entries) -> std::vector<std::string_view>
        {
            std::vector<std::string_view> names;

            for( const auto & entry : entries)
            {
                if( std::ranges::find( names, entry.Site) == names.end())
                {
                    names.push_back( entry.Site);
                }
            }

            return names;
        }
    } // namespace detail

    auto poolFor( const std::string_view box) -> std::vector<Address>
    {
        return detail::poolIn( detail::poolEntries, box);
    }

    auto siteAddressFor( const std::string_view site, const std::string_view box) -> std::optional<Address>
    {
        return detail::siteAddressIn( detail::siteEntries, site, box);
    }

    auto instrumentBoxNames() -> std::vector<std::string_view>
    {
        std::vector<std::string_view> names;

        for( const auto & row : detail::instrumentBoxRows)
        {
            if( std::ranges::find( names, row.Box) == names.end())
            {
                names.push_back( row.Box);
            }
        }

        return names;
    }

    auto siteNames() -> std::vector<std::string_view>
    {
        return detail::siteNamesIn( detail::siteEntries);
    }
} // namespace hal
