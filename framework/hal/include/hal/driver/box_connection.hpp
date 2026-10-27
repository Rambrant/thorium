#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <stdexcept>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"
#include "hal/io/transport.hpp"

//
// One box, one session -- owned here rather than by each driver.
//
// A box (hal/topology/boxes.hpp) is the physical unit a rig's rows are faces
// of, and it is the box that has a socket or a USBTMC session, not the face. So
// every face of one box -- the three outputs of a supply, the meter and the
// switching of a switch/measure mainframe -- talks through one SCPI session:
// one error queue, one identity check, and no second connection for the
// instrument to refuse.
//
// Every driver that talks holds a BoxConnection in place of the session
// members each of them used to keep for itself, and the supply's own
// address-keyed sharing (the first version of this, in
// hal::keysight_edu36311a) is now this, for everyone.
//
//     class MyDriver : public InstrumentTag
//     {
//         MyDriver( InstrumentId id, Usb address) : mConnection( id, address) {}
//
//         auto session() -> io::ScpiSession &
//         {
//             return mConnection.session( "my_package", []( io::ScpiSession & s) { prepare( s); });
//         }
//
//         BoxConnection mConnection;
//     };
//
// The rules, each one a property some driver already had and every driver now
// has the same way:
//
//   shared per box and address  faces of one box at one address share; a face
//                               moved elsewhere (useAddress) leaves its
//                               siblings on the old session. A Simulated
//                               address names no unit and shares nothing.
//
//   opened lazily               on the first use by *any* face, never in a
//                               constructor -- a rig's instruments are globals
//                               constructed before main().
//
//   prepared once per family    the once-per-session exchange (the error-queue
//                               drain, the model check) runs once per driver
//                               *family* on a session, not once per face: the
//                               three outputs of a supply are three types and
//                               one family, so the second output does not
//                               drain an error the first has just queued. Two
//                               families on one box -- a mainframe's switching
//                               and its meter -- each prepare their own.
//
//   closed per box              close() closes the session for every face,
//                               because recovering a wedged box is closing its
//                               one connection, not one of several.
//
//   never opened by safing      openSession() is what a safe() uses: the
//                               session if some face opened it, else nothing.
//
// -- A box that is not SCPI ---------------------------------------------------
//
// Everything above is about an io::ScpiSession, and a unit with no SCPI -- one
// driven through the vendor's own library, a handle and a function table -- is
// still one physical unit behind one connection, with faces that must share it
// (hal::pico_5244d: the scope and its generator). So the link also holds one
// *native* object of a type the driver chooses, shared, lazily made, found by
// safing without being made, and released by close(), with exactly the rules
// above:
//
//     auto & unit = mConnection.native<MyUnit>( []{ return std::make_shared<MyUnit>(); });
//     auto * open = mConnection.openNative<MyUnit>();     // safing
//
// A box holds a session or a native object, not both; asking for the other
// kind of a box that holds one is a std::logic_error, because it means two
// families disagree about what the unit is.
//
namespace hal
{
    namespace detail
    {
        //
        // The session a box has at one address, and which driver families
        // have prepared it.
        //
        struct BoxLink
        {
            std::unique_ptr<io::ScpiSession>  Session;
            std::vector<std::string>          PreparedBy;

            //
            // A native driver's one shared object, type-erased, and the type it
            // was made as -- checked on every access, so two families that
            // disagree about it fail loudly instead of reinterpreting memory.
            //
            std::shared_ptr<void>             Native;
            const std::type_info *            NativeType{ nullptr };
        };

        //
        // The link for this box at this address: the same object for every
        // caller naming both, for as long as any of them holds it, and a fresh
        // one once none does. Never called for a Simulated address.
        //
        [[nodiscard]]
        auto boxLinkAt( std::string_view box, const Address & address) -> std::shared_ptr<BoxLink>;

        //
        // The same, without creating one -- null when no face holds it.
        //
        [[nodiscard]]
        auto existingBoxLinkAt( std::string_view box, const Address & address) -> std::shared_ptr<BoxLink>;
    } // namespace detail

    class BoxConnection
    {
        public:
            BoxConnection( const InstrumentId id, Address address) : mId( id), mAddress( std::move( address)) {}

            //
            // Move-only, as the std::unique_ptr<io::ScpiSession> every driver
            // held before this was: a copy would be a second face claiming to
            // be this one, holding the same link under a different object,
            // where a move hands the one hold over.
            //
            BoxConnection( const BoxConnection &)                    = delete;
            auto operator=( const BoxConnection &) -> BoxConnection & = delete;

            BoxConnection( BoxConnection &&)                    = default;
            auto operator=( BoxConnection &&) -> BoxConnection & = default;

            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            //
            // The box this face is on -- the first column of its row.
            //
            [[nodiscard]]
            auto box() const -> std::string_view;

            //
            // A Simulated address and no transport handed in: nothing at the
            // other end, and every driver's simulation branch.
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return !hasSession() && std::holds_alternative<Simulated>( mAddress);
            }

            //
            // Whether this face's box has a session open -- by this face or
            // any other. Looked up, not assumed: a face that has never spoken
            // still sees a session its sibling opened. Never creates one.
            //
            [[nodiscard]]
            auto hasSession() const -> bool
            {
                const auto * held = heldLink();

                return held && ( held->Session || held->Native);
            }

            //
            // Talk through this transport instead of one opened from the
            // address -- installed on the box, so every face of it at this
            // address uses it. Sends nothing; the preparing belongs to the
            // first use.
            //
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void;

            //
            // Move this face to another address -- what preflight does with an
            // --address. Drops only this face's hold on the old link; its
            // siblings keep theirs, and the new address is found on next use.
            //
            auto useAddress( const Address & address) -> void
            {
                mAddress = address;
                mLink.reset();
            }

            //
            // The live session, opened from the address on first use and
            // prepared once for this family. Throws hal::io::TransportError if
            // the box cannot be reached, and whatever prepare throws -- after
            // which it is not marked prepared, so the next use asks again.
            //
            template<std::invocable<io::ScpiSession &> Prepare>
            auto session( const std::string_view family, Prepare && prepare) -> io::ScpiSession &
            {
                auto & held = link();

                if( held.Native)
                {
                    throw std::logic_error( "BoxConnection: box " + std::string( box()) + " holds a native object, not an SCPI session");
                }

                if( !held.Session)
                {
                    held.Session = std::make_unique<io::ScpiSession>( io::openTransport( mAddress));
                }

                if( !preparedBy( held, family))
                {
                    std::forward<Prepare>( prepare)( *held.Session);

                    held.PreparedBy.emplace_back( family);
                }

                return *held.Session;
            }

            //
            // The box's native object, made by `make` on first use by any face
            // (a callable returning std::shared_ptr<T>) and the same object
            // for every face after. Not made in a constructor, for the reason
            // session() is not opened in one.
            //
            template<typename T, std::invocable<> Make>
            auto native( Make && make) -> T &
            {
                auto & held = link();

                if( held.Session)
                {
                    throw std::logic_error( "BoxConnection: box " + std::string( box()) + " holds an SCPI session, not a native object");
                }

                if( !held.Native)
                {
                    held.Native     = std::forward<Make>( make)();
                    held.NativeType = &typeid( T);
                }
                else if( *held.NativeType != typeid( T))
                {
                    throw std::logic_error( "BoxConnection: box " + std::string( box()) + " holds a different native type");
                }

                return *static_cast<T *>( held.Native.get());
            }

            //
            // The same object if some face has made it, or null -- never making
            // one. What a native driver's safe() uses.
            //
            template<typename T>
            [[nodiscard]]
            auto openNative() -> T *
            {
                auto * const held = heldLink();

                if( !held || !held->Native)
                {
                    return nullptr;
                }

                return *held->NativeType == typeid( T) ? static_cast<T *>( held->Native.get()) : nullptr;
            }

            //
            // Put this object on the box in place of whatever it holds --
            // what a native driver's test seam does, as useTransport() does for
            // an SCPI one. Prepared families are forgotten.
            //
            template<typename T>
            auto installNative( std::shared_ptr<T> object) -> void
            {
                auto & held = link();

                held.Session.reset();
                held.Native     = std::move( object);
                held.NativeType = &typeid( T);
                held.PreparedBy.clear();
            }

            //
            // The session if some face of this box has opened it, or null --
            // never opening one. What safing uses.
            //
            [[nodiscard]]
            auto openSession() -> io::ScpiSession *;

            //
            // Close the box's session, for every face. The next command by any
            // of them opens a new one.
            //
            auto close() -> void;

        private:
            //
            // The link, found on first need rather than in the constructor: the
            // registry is not something to touch before main(). A Simulated
            // address gets one of its own -- it names no unit, so nothing to
            // share it with.
            //
            auto link() -> detail::BoxLink &;

            //
            // The link if one exists for this box and address, found without
            // creating anything -- what hasSession(), openSession() and
            // close() all ask, so that a face that has not spoken still sees,
            // uses and closes what its siblings opened. Remembers what it
            // found, which is why mLink is mutable: finding is not a change
            // anyone outside could observe, only a lookup not repeated.
            //
            auto heldLink() const -> detail::BoxLink *;

            [[nodiscard]]
            static auto preparedBy( const detail::BoxLink & held, std::string_view family) -> bool;

            InstrumentId                               mId;
            Address                                    mAddress;
            mutable std::shared_ptr<detail::BoxLink>   mLink;
    };
} // namespace hal
