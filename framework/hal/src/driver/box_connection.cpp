#include "hal/driver/box_connection.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "hal/topology/boxes.hpp"

namespace hal
{
    namespace detail
    {
        namespace
        {
            //
            // Every link some face holds, by box and address.
            //
            // Weak, so the registry never keeps a session open by itself: a link
            // lives exactly as long as some face holds it. Keyed on the box name
            // and hal::to_string( address) -- owned strings, because hal::Lan
            // and hal::Usb view their text rather than owning it, and the face
            // that first registered a link may move or be destroyed while its
            // siblings still hold it.
            //
            // Locked, although a run drives its instruments from one thread: the
            // registry is cheap to protect and costly to corrupt. A link it hands
            // back is not locked -- one box's SCPI exchanges are as sequential as
            // they always were.
            //
            struct Registry
            {
                struct Entry
                {
                    std::string              Box;
                    std::string              Address;
                    std::weak_ptr<BoxLink>   Link;
                };

                std::mutex          Guard;
                std::vector<Entry>  Links;

                auto find( const std::string_view box, const std::string & address) -> std::shared_ptr<BoxLink>
                {
                    std::erase_if( Links, []( const Entry & entry) { return entry.Link.expired(); });

                    const auto found = std::ranges::find_if( Links, [ & ]( const Entry & entry)
                    {
                        return entry.Box == box && entry.Address == address;
                    });

                    return found == Links.end() ? nullptr : found->Link.lock();
                }
            };

            auto registry() -> Registry &
            {
                static Registry instance;

                return instance;
            }
        } // namespace

        auto boxLinkAt( const std::string_view box, const Address & address) -> std::shared_ptr<BoxLink>
        {
            auto &                reg = registry();
            const std::lock_guard lock( reg.Guard);
            const auto            key = to_string( address);

            if( auto link = reg.find( box, key))
            {
                return link;
            }

            auto link = std::make_shared<BoxLink>();

            reg.Links.push_back( Registry::Entry{ std::string( box), key, link });

            return link;
        }

        auto existingBoxLinkAt( const std::string_view box, const Address & address) -> std::shared_ptr<BoxLink>
        {
            auto &                reg = registry();
            const std::lock_guard lock( reg.Guard);

            return reg.find( box, to_string( address));
        }
    } // namespace detail

    auto BoxConnection::box() const -> std::string_view
    {
        return boxOf( mId);
    }

    auto BoxConnection::link() -> detail::BoxLink &
    {
        if( !mLink)
        {
            mLink = std::holds_alternative<Simulated>( mAddress)
                        ? std::make_shared<detail::BoxLink>()
                        : detail::boxLinkAt( box(), mAddress);
        }

        return *mLink;
    }

    auto BoxConnection::heldLink() const -> detail::BoxLink *
    {
        if( !mLink && !std::holds_alternative<Simulated>( mAddress))
        {
            mLink = detail::existingBoxLinkAt( box(), mAddress);
        }

        return mLink.get();
    }

    auto BoxConnection::preparedBy( const detail::BoxLink & held, const std::string_view family) -> bool
    {
        return std::ranges::find( held.PreparedBy, family) != held.PreparedBy.end();
    }

    auto BoxConnection::useTransport( std::unique_ptr<io::ITransport> transport) -> void
    {
        auto & held = link();

        held.Session = std::make_unique<io::ScpiSession>( std::move( transport));
        held.PreparedBy.clear();
    }

    auto BoxConnection::openSession() -> io::ScpiSession *
    {
        //
        // Found, never created: safing must never cause a session to exist.
        //
        auto * const held = heldLink();

        return held && held->Session ? held->Session.get() : nullptr;
    }

    auto BoxConnection::close() -> void
    {
        //
        // Found the way openSession() finds it, so a face that has not spoken
        // still closes a session its siblings opened -- and never creates one
        // to close.
        //
        if( auto * const held = heldLink())
        {
            held->Session.reset();
            held->PreparedBy.clear();
        }
    }
} // namespace hal
