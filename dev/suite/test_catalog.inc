#pragma once

//
// The dev suite's catalog -- one group, one test. Read exactly as
// suite/test_catalog.inc is; see suite/README.md for GROUP/TEST and the
// RUN_SETUP/RUN_TEARDOWN and per-group SETUP/TEARDOWN hooks.
//
// No RUN_SETUP or RUN_TEARDOWN, and that is the difference worth noticing. The
// bench suite brackets every run with rigPowerOn/rigPowerOff, because a DUT has
// to be brought up before anything can be measured and must not be left
// energised. This bench sources nothing: there is no rail to raise, so a hook
// pair here would be two scripts with empty bodies. Safing still happens on the
// way out -- that is hal::safeRig() in the runner, not a catalog hook.
//
GROUP( BenchSanity, "Checks that the bench meter itself is behaving")
    TEST( DmmSelfCheck, dmmSelfCheck, "The meter reads a known reference within tolerance")
END_GROUP
