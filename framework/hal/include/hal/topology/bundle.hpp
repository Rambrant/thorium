#pragma once

#include <meta>
#include <vector>

#include "core/topology/adapter.hpp"
#include "hal/topology/vpc_location.hpp"

namespace hal
{
    //
    // Every LINE in a bundle, as the VpcLocation it lands on -- what a routing
    // verb needs to turn Connect( Ser1.rs232(), at( dut::Console)) into the set
    // of connector channels to close (see core::ConnectEngine's bundle overload
    // in core/verbs/route.hpp).
    //
    // Reflection over the bundle's own members, rather than anything the BUNDLE
    // macro accumulates. That is the whole reason bundles route as a unit at
    // all: a macro cannot build a list across its own invocations, so a LINE
    // added to a bundle would otherwise have to be repeated somewhere a router
    // could see it -- and the copy that nobody updated is exactly the failure
    // this codebase spends its complexity avoiding. Here there is no copy: the
    // declarations in dut/adapter.inc *are* the list.
    //
    // This lives in hal rather than in core/topology/adapter.hpp beside AdapterBundle,
    // because it is the one part that has to name a concrete coordinate type.
    // core knows only that a point's location is "some structural value usable
    // as a template parameter" (see core::AdapterPointTag); VpcLocation is this
    // family of rigs' answer, and hal is where that answer is already known.
    //
    // Deliberately flat -- it does not recurse into a bundle nested inside a
    // bundle. No adapter declares one today, and a nested interface is a
    // question about what routing it should mean (all of it? the outer lines
    // only?) rather than a walk that was left unfinished. If one ever appears,
    // the answer belongs here as a deliberate decision, not as whatever a
    // recursive walk happened to do. dut/tests/test_wiring_coverage.cpp does
    // recurse, and correctly: every pin must be covered wherever it is
    // declared, which is a different question with an obvious answer.
    //
    template<typename BundleT>
    consteval auto bundleLocationList() -> std::vector<VpcLocation>
    {
        std::vector<VpcLocation> result;

        for( const auto member : std::meta::members_of( ^^BundleT, std::meta::access_context::current()))
        {
            //
            // is_variable filters out the injected-class-name (a struct is a
            // member of itself) and any nested type, so the walk needs no
            // visited-set the way the recursive one in
            // dut/tests/test_wiring_coverage.cpp does.
            //
            if( !std::meta::is_variable( member))
            {
                continue;
            }

            const auto type = std::meta::remove_cv( std::meta::type_of( member));

            if( !std::meta::has_template_arguments( type))
            {
                continue;
            }

            if( std::meta::template_of( type) != ^^core::AdapterPointTag)
            {
                continue;
            }

            //
            // template_arguments_of is (Loc, Kind) -- see core::AdapterPointTag.
            // Kind is not filtered on: a bundle of SOURCE_LINEs (dut::AcInput)
            // and one of LINEs (dut::Console) both name pins, and which table
            // those pins are reachable through is the caller's question, not
            // this walk's.
            //
            result.push_back( std::meta::extract<VpcLocation>( std::meta::template_arguments_of( type)[ 0]));
        }

        return result;
    }

    //
    // The spelling a router uses: a static array of this bundle's locations,
    // fixed at compile time.
    //
    // Takes the AdapterBundle value's type, not the bundle struct, because that
    // is what an at( dut::Console) call site actually produced -- see
    // core::AdapterBundle on why the two share a name and why naming the struct
    // directly would need the elaborated `struct dut::Console`.
    //
    template<typename BundleT>
    constexpr auto bundleLocations = std::define_static_array( bundleLocationList<BundleT>());
} // namespace hal
