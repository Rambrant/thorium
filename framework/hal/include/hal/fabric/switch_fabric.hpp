#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "hal/fabric/switch_device.hpp"

namespace hal
{
    //
    // One switchable channel: a specific channel on a specific matrix card or
    // mux. This is deliberately just plumbing -- unlike hal::Instrument, it
    // has no quantity type and nothing to read -- so the type system can never
    // let a script try to Measure a mux channel.
    //
    // Two fields, not three. What kind of hardware the channel lives on is a
    // property of the device and is stated once where the device is declared
    // (see hal::kindOf and rig/devices.inc), so no two hops can disagree about
    // one card -- which they could, and silently, when each hop carried its own
    // kind alongside a device name in a bare string. hal/fabric/switch_device.hpp's
    // own comment spells out both failure modes that removed.
    //
    struct SwitchElementId
    {
        SwitchDeviceId device;
        std::uint16_t  channel;

        friend constexpr auto operator==( SwitchElementId, SwitchElementId) -> bool = default;
        friend constexpr auto operator<=>( SwitchElementId, SwitchElementId) = default;
    };

    [[nodiscard]]
    auto to_string( SwitchElementId id) -> std::string;

    //
    // The three checked spellings of "channel N of card C" -- what HOP,
    // CROSSPOINT and BANK expand to (see hal/topology/wiring.hpp for the macros, and a
    // rig's own wiring.inc for the tables written in them).
    //
    // Function templates rather than a braced SwitchElementId{ ... } because
    // the channel has to be a *constant* for anything to be checked about it,
    // and a template argument is the only place a value is one. That is the
    // whole of the difference: hop<Mux1, 3>() is the same two fields it always
    // was, plus a static_assert the rig cannot skip.
    //
    // constexpr, deliberately not consteval, even though every real call site
    // is a constant. A rig's CONNECTOR_WIRING expands inside
    // detail::buildConnectorWiringEntries(), which is constexpr because
    // hal::connectorWiring calls it at ordinary runtime -- and a consteval
    // call anywhere in that body would promote the whole builder to an
    // immediate function and break that call (see hal/topology/wiring.hpp's own
    // comment on why the builder is constexpr). The static_assert fires at
    // instantiation either way, which is what the check needs.
    //
    // What the check catches is not a typo in the card's name -- SwitchDeviceId
    // already made that a compile error -- but a channel number that card does
    // not have: HOP( Spdt1, 300) on an 80-channel relay card, or a 1260-45
    // crosspoint written with a column of 30 on a card whose columns stop at
    // 15. Before there was a model column there was nothing to check it
    // against, and such a hop produced a fabric element for a relay that does
    // not exist: closed, opened, and routing nothing, with every table
    // reading as complete.
    //
    template<SwitchDeviceId Device, std::uint16_t Channel>
    [[nodiscard]]
    constexpr auto hop() -> SwitchElementId
    {
        static_assert( hasChannel( Device, Channel),
                       std::string( "not a channel of this card -- ") + std::string( partOf( Device)) +
                       " has " + std::string( channelsOf( Device)));

        return SwitchElementId{ Device, Channel };
    }

    //
    // The same hop, written as the card numbers it. A 1260-45 channel is
    // <group><row><column> -- 2312 is group 2, row 3, column 12 -- and a
    // rig writing that as one four-digit literal has two problems the parts
    // don't: it says nothing at the call site about which crosspoint it is,
    // and a leading zero makes it octal. CROSSPOINT( Matrix1, 0, 3, 0) has
    // neither, and reads as the thing the wiring diagram shows.
    //
    // Only a card whose spec carries the scheme can be written this way, so
    // CROSSPOINT on a mux is a compile error rather than arithmetic that
    // happens to produce a number. Same for BANK below, which is the
    // E1472A's <bank><channel>.
    //
    template<SwitchDeviceId Device, unsigned Group, unsigned Row, unsigned Column>
    [[nodiscard]]
    constexpr auto crosspoint() -> SwitchElementId
    {
        static_assert( specOf( modelOf( Device)).Crosspoint != nullptr,
                       std::string( "this card has no group/row/column numbering -- ") +
                       std::string( partOf( Device)) + " has " + std::string( channelsOf( Device)));

        //
        // Guarded so a card without the scheme fails on the assertion above
        // and nothing else: the call below would otherwise be a null function
        // pointer invoked during constant evaluation, which reports as its own
        // error on top of the one worth reading.
        //
        if constexpr( specOf( modelOf( Device)).Crosspoint != nullptr)
        {
            return hop<Device, specOf( modelOf( Device)).Crosspoint( Group, Row, Column)>();
        }
        else
        {
            return SwitchElementId{ Device, 0 };
        }
    }

    //
    // The matrix crosspoint written as a row and a column, for a card that
    // numbers its crosspoints by those two and nothing else -- a Keysight
    // 34932A's 315 is row 3, column 15.
    //
    // A third spelling rather than crosspoint() with a group of zero, because
    // a group this card does not have is not a group whose value is zero: the
    // four-argument form on a 34932A would read as "group 0" and leave a
    // reader wondering which of four groups that is, on a card with none. The
    // 1260-45A genuinely has four independent matrices selected by a group
    // digit; the 34932A has two selected by which half of the row axis you
    // are on (rows 1-4 are Matrix 1, rows 5-8 are Matrix 2), which is a fact
    // about the rows rather than a separate coordinate.
    //
    // Only a card whose spec carries the scheme can be written this way, so
    // ROW_COLUMN on a mux, or on the 1260-45A, is a compile error rather than
    // arithmetic that happens to produce a number.
    //
    template<SwitchDeviceId Device, unsigned Row, unsigned Column>
    [[nodiscard]]
    constexpr auto rowColumn() -> SwitchElementId
    {
        static_assert( specOf( modelOf( Device)).RowColumn != nullptr,
                       std::string( "this card has no plain row/column numbering -- ") +
                       std::string( partOf( Device)) + " has " + std::string( channelsOf( Device)));

        //
        // Guarded so a card without the scheme fails on the assertion above
        // and nothing else -- see crosspoint() for the null-function-pointer
        // problem this avoids.
        //
        if constexpr( specOf( modelOf( Device)).RowColumn != nullptr)
        {
            return hop<Device, specOf( modelOf( Device)).RowColumn( Row, Column)>();
        }
        else
        {
            return SwitchElementId{ Device, 0 };
        }
    }

    template<SwitchDeviceId Device, unsigned Bank, unsigned Channel>
    [[nodiscard]]
    constexpr auto bank() -> SwitchElementId
    {
        static_assert( specOf( modelOf( Device)).BankChannel != nullptr,
                       std::string( "this card has no bank/channel numbering -- ") +
                       std::string( partOf( Device)) + " has " + std::string( channelsOf( Device)));

        //
        // Guarded so a card without the scheme fails on the assertion above
        // and nothing else: the call below would otherwise be a null function
        // pointer invoked during constant evaluation, which reports as its own
        // error on top of the one worth reading.
        //
        if constexpr( specOf( modelOf( Device)).BankChannel != nullptr)
        {
            return hop<Device, specOf( modelOf( Device)).BankChannel( Bank, Channel)>();
        }
        else
        {
            return SwitchElementId{ Device, 0 };
        }
    }

    //
    // A route through the fabric between two fixed points is rarely just
    // one relay -- the real wiring behind hal::InstrumentWiring/
    // hal::ConnectorWiring's entries can be a chain of several (a mux
    // narrowing thousands of DUT points down to a handful of lines, then
    // another mux narrowing further, then finally a matrix crosspoint --
    // see those two classes' own comments). SwitchFabric::connect()/
    // disconnect() already take a plain std::vector<SwitchElementId> and
    // don't care how many elements are in it or which wiring fact
    // contributed which one -- every element in the chain just needs
    // closing (or opening) together. Path names that vector at the type
    // level so a chain and a single hop are the same thing to write and to
    // compose (see hal::InstrumentWiring::find()/findAll() and
    // core::MeasureEngine's operator(), which concatenates an instrument's
    // Path with a connector's Path into the one combined route).
    //
    using Path = std::vector<SwitchElementId>;

    //
    // What moves a relay, when the fabric decides one should move.
    //
    // The fabric decides *when* -- an element's first use closes it, its last
    // release opens it -- and a driver does the moving. Abstract here so the
    // fabric stays plumbing (see the class below on why it must not know about
    // instruments): the rig supplies one that finds, for an element's card, the
    // instrument on the same box that switches it (hal/fabric/rig_switching.hpp).
    //
    // Either call may throw -- a refused ROUT:CLOS is an hal::io::ScpiFault --
    // and the fabric keeps its count true when one does: an element whose
    // physical close failed is not counted as closed.
    //
    class SwitchDriver
    {
        public:
            virtual ~SwitchDriver() = default;

            virtual auto close( SwitchElementId id) -> void = 0;
            virtual auto open( SwitchElementId id) -> void = 0;
    };

    //
    // The switching fabric sitting between the instruments and the VPC array:
    // every card the rig's devices.inc declares (see hal/fabric/switch_device.hpp),
    // addressed uniformly by SwitchElementId.
    //
    // Bookkeeping always, and hardware when it has a driver: handed a
    // SwitchDriver, close() and open() move the real relay at the moments the
    // use count says a relay should move, and not otherwise. Without one --
    // the default, and every card on a bench whose switching has no driver yet
    // -- it only tracks state, so routing logic can be exercised and asserted
    // on with nothing behind it.
    //
    // What this class deliberately does NOT do is decide whether a path is
    // electrically safe to close, even though README.md once called that "a
    // runtime interlock the fabric does not yet have". It cannot: an element is
    // a device and a channel, and the two hazards worth refusing are about
    // things no SwitchElementId carries -- which VPC pin the route lands on,
    // which instrument is cabled onto that pin, whether that instrument's
    // output is on, and what quantity the reading at the far end is even for.
    // Pushing all four in here would make the plumbing know about instruments
    // and quantities, which is exactly the coupling SwitchElementId's own
    // comment above exists to prevent.
    //
    // So the interlock sits where each of those facts already is: the routing
    // verbs ask the driver whether it is live (core/verbs/route.hpp), and the routed
    // Measure asks the rig which source lands on the pin it is about to reach
    // (core/verbs/measure.hpp, hal/verbs/interlock.hpp). See core/verbs/interlock.hpp for the
    // whole argument.
    //
    // Each element's state is a use count, not a plain bool: a physical
    // relay is either open or closed, but two independent callers can both
    // legitimately need it closed at once -- e.g. Connect() holding a
    // supply on a DUT point while Measure() briefly listens in on that
    // same point with a second instrument. Whoever asked for it closed
    // last is not necessarily who's done with it first, so open() only
    // actually opens the relay once every close() on it has been matched
    // by an open() -- which is also exactly when the driver is told to open
    // it, and a close() on an element already closed tells the driver nothing.
    //
    class SwitchFabric
    {
        public:
            SwitchFabric() = default;

            explicit SwitchFabric( SwitchDriver & driver) : mDriver( &driver) {}

            //
            // One use more. The first use closes the relay; the count is
            // taken only once that has succeeded, so a refused close leaves
            // the element open on the books as it is on the bench.
            //
            auto close( SwitchElementId id) -> void
            {
                const auto found = mUseCount.find( id);

                if( found != mUseCount.end())
                {
                    ++found->second;

                    return;
                }

                if( mDriver)
                {
                    mDriver->close( id);
                }

                mUseCount[ id] = 1;
            }

            //
            // One use fewer. The last release opens the relay -- and the
            // count is dropped first, so a relay whose physical open failed is
            // not left counted as somebody's: nobody holds it any more, and a
            // later close must try again rather than believe it is closed.
            //
            auto open( SwitchElementId id) -> void
            {
                const auto found = mUseCount.find( id);

                if( found == mUseCount.end())
                {
                    return;
                }

                if( --found->second > 0)
                {
                    return;
                }

                mUseCount.erase( found);

                if( mDriver)
                {
                    mDriver->open( id);
                }
            }

            //
            // Forget every use -- bookkeeping only, and deliberately so. This
            // is hal::safeRig()'s last step, and by then safing's relay pass
            // has already opened the hardware through each relay-holding
            // instrument (see hal::RelayHoldingInstrument). Opening it again
            // here would mean telling a driver to talk during safing, which
            // may not open a session -- so the hardware half has one home,
            // and this is not it.
            //
            auto openAll() -> void
            {
                mUseCount.clear();
            }

            [[nodiscard]]
            auto isClosed( SwitchElementId id) const -> bool
            {
                return mUseCount.count( id) > 0;
            }

            //
            // connect()/disconnect(): close (or open) exactly the given
            // path's elements, one use each -- additive, leaving every
            // other currently-closed element (and every other use of these
            // same elements) untouched. This is what core::ConnectEngine/
            // DisconnectEngine and core::MeasureEngine call (see
            // core/verbs/route.hpp, core/verbs/measure.hpp): a source instrument's own
            // relay path shouldn't tear down some other already-live route
            // (another instrument's supply, say) just because it's being
            // connected, disconnected, or briefly shared with a
            // measurement -- and thanks to the use-count above, two
            // callers sharing one element (e.g. a supply's connector
            // channel also being read by a DMM) leave it closed until both
            // have released it, not just the first one to disconnect.
            //
            // A path is all or nothing: if one element's close fails, the
            // elements this call already closed are released again before
            // the failure goes on, so a refused crosspoint cannot leave half
            // a route closed -- a bus relay onto a meter, say, with nothing
            // behind it that anyone will ever release.
            //
            auto connect( const std::vector<SwitchElementId> & path) -> void
            {
                std::size_t closed = 0;

                try
                {
                    for( const auto id : path)
                    {
                        close( id);

                        ++closed;
                    }
                }
                catch( ...)
                {
                    for( std::size_t index = closed; index > 0; --index)
                    {
                        try
                        {
                            open( path[ index - 1]);
                        }
                        catch( ...)
                        {
                            //
                            // The original failure is the one to report; a
                            // second one while undoing it says less.
                            //
                        }
                    }

                    throw;
                }
            }

            //
            // And a release is all of the path even when part of it fails:
            // every element is released, and the first failure is reported
            // once they all have been. Stopping at the first would leave the
            // rest of the route counted as held by a caller that has finished
            // with it -- closed for good, as far as the fabric could tell.
            //
            auto disconnect( const std::vector<SwitchElementId> & path) -> void
            {
                std::exception_ptr first;

                for( const auto id : path)
                {
                    try
                    {
                        open( id);
                    }
                    catch( ...)
                    {
                        if( !first)
                        {
                            first = std::current_exception();
                        }
                    }
                }

                if( first)
                {
                    std::rethrow_exception( first);
                }
            }

        private:
            SwitchDriver *                  mDriver{ nullptr };
            std::map<SwitchElementId, int>  mUseCount;
    };
} // namespace hal
