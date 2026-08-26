# -*- coding: utf-8 -*-
# The deck as content: (layout number, {placeholder-key: content}).
# Placeholder keys: 'kicker' (small label above title), 'title', 'body',
# 'left', 'right', 'sub' (title-slide subtitle), 'quote', 'attrib'.
# Content is a list of (level, [(text, bold), ...]) paragraphs, or a plain string.
#
# Two acts, in this order, and the order is the point:
#
#   Act 1 -- what a test writer gets. What they write, what they never write,
#            what happens when they get it wrong, and how they work without a
#            bench. Nothing here requires knowing how the framework is built.
#   Act 2 -- the mechanisms that make Act 1 possible. Read second, because a
#            mechanism is only interesting once you have seen what it buys.
#
# Figures in here are checked against the tree, not remembered: 402 tests and
# 45 acceptance tests (ctest), 4 drivers, 4 scripts, 3 criteria variants.

FOOTER = "Information Classification: INTERNAL"

def p(text, lvl=0, accent=False):
    """A bulleted line."""
    return (lvl, [(text, False, accent)], 'bullet')

def note(text, lvl=0, accent=False):
    """An un-bulleted line -- a lead-in or a closing statement, not a list item.

    lvl picks up the layout's size for that outline level (the Conclusion layout,
    for instance, sets 32pt for level 1 and 18pt for level 2).
    """
    return (lvl, [(text, False, accent)], 'note')

def code(*lines):
    """A monospaced block, indentation preserved, no bullets."""
    return [(0, [(l, False, True)], 'code') for l in lines]

def rich(lvl, *runs):
    return (lvl, list(runs), 'bullet')

def richnote(*runs):
    return (0, list(runs), 'note')

DECK = [
    # 1 -------------------------------------------------- title
    (1, {
        'title': "Thorium",
        'sub':   "Writing hardware tests that cannot lie to you\nThomas Rambrant  ·  Knowit Connectivity",
    }),

    # 2 -------------------------------------------------- divider
    (6, {
        'kicker': "Why bother",
        'title':  "A test suite that passes is not the same as a DUT that works",
    }),

    # 3 -------------------------------------------------- the pain
    (14, {
        'kicker': "Why bother",
        'title':  "Four ways a suite lies to you",
        'body': [
            p("A mistyped test point measures the wrong pin — and passes"),
            p("A criterion compared against the wrong unit — and passes"),
            p("A relay path nobody wired up — throws three hours into a soak run"),
            p("A tolerance edited for one variant, not the others"),
            p(""),
            note("Every one of these is a static fact about your test.", accent=True),
            note("None of them needs a powered rig to discover."),
        ],
    }),

    # 4 -------------------------------------------------- the rule
    (19, {
        'quote':  "If a mistake can be caught by the compiler,\nit must be caught by the compiler.",
        'attrib': "Runtime checks are a last resort — and every one of them is a documented concession.",
    }),

    # 5 ================================================== ACT 1
    (6, {
        'kicker': "Act 1",
        'title':  "What you get, as the person writing the tests",
    }),

    # 6 -------------------------------------------------- the whole job
    (14, {
        'kicker': "Writing a test",
        'title':  "Three files, and none of them are machinery",
        'body': [
            rich(0, ("The tolerance. ", False, True), ("A row in your DUT's criteria table — value, unit, and the wording from the spec.", False, False)),
            rich(0, ("The script. ", False, True), ("Measure, verify, done. Reads like the procedure it came from.", False, False)),
            rich(0, ("The catalog line. ", False, True), ("One row naming the group, the id and the description an operator will see.", False, False)),
            p(""),
            note("Everything else — addresses, relays, units, logging — already exists.", accent=True),
        ],
    }),

    # 7 -------------------------------------------------- code + what's hidden
    (15, {
        'kicker': "Writing a test",
        'title':  "What a script never mentions",
        'left': [
            richnote(("The whole test", False, True)),
            p(""),
            *code("auto rail = Measure(",
                  "    Dmm1.voltage(),",
                  "    at( dut::Output5V));",
                  "",
                  "Verify(",
                  "    FS_Supply_1::FS_Supply_5V0,",
                  "    rail);"),
        ],
        'right': [
            richnote(("Nowhere in it", False, True)),
            p(""),
            p("Instrument address"),
            p("Relay path or channel numbers"),
            p("Unit conversion"),
            p("The tolerance value itself"),
            p("A single logging call"),
            p(""),
            note("All resolved elsewhere — most of it by the compiler.", accent=True),
        ],
    }),

    # 8 -------------------------------------------------- the tables
    (15, {
        'kicker': "Writing a test",
        'title':  "Your tables stay tables",
        'left': [
            *code("CRIT( FS_Supply_5V0,",
                  "  EQ( 5.0_V)",
                  "    .epsilon( 0.05_V),",
                  '  "5Vdc port, 50mV")',
                  "",
                  "POINT( Output5V,",
                  "  A, 1, 3,",
                  '  "5Vdc supply port")'),
        ],
        'right': [
            p("Tolerances, test points, the catalog, the rig's wiring — flat tabular files"),
            p("Meant to be read by a test engineer, close to the spec spreadsheet"),
            p("The units are real: 5.0_V is a Voltage, not a double"),
            p(""),
            note("Data you edit. The machinery that reads it lives somewhere you never open.", accent=True),
        ],
    }),

    # 9 -------------------------------------------------- getting it wrong
    (15, {
        'kicker': "Writing a test",
        'title':  "What happens when you get it wrong",
        # The two columns are read as rows, so every entry on the right has to
        # fit one line -- one wrap and the pairing silently drifts apart.
        'left': [
            richnote(("You write", False, True)),
            p(""),
            p("dut::Output5Vx"),
            p("FS_Supply_5V0x"),
            p("EQ( 5.0_A) on a voltage"),
            p("A point nobody wired"),
            p("A limit in one variant only"),
            p("Connect on a relay-less supply"),
        ],
        'right': [
            richnote(("You are told", False, True)),
            p(""),
            p("no such member"),
            p("no such member"),
            p("no viable predicate"),
            p("build stops, names the point"),
            p("build stops, names the variant"),
            p("no matching function"),
            p(""),
            note("Every variant is checked, not just the one you build against.", accent=True),
        ],
    }),

    # 10 ------------------------------------------------- no rig needed
    (14, {
        'kicker': "Working",
        'title':  "You do not need the rig to write the test",
        'body': [
            rich(0, ("Inject. ", False, True), ("Feed a script the readings you want and check it reaches the verdict you expect.", False, False)),
            rich(0, ("Record. ", False, True), ("Capture every reading a real run took, in order.", False, False)),
            rich(0, ("Replay. ", False, True), ("Re-run the suite against that file — same verdict, no hardware, no bench time.", False, False)),
            p(""),
            note("A failure seen once on the bench becomes a test anyone can re-run at a desk.", accent=True),
        ],
    }),

    # 11 ------------------------------------------------- variants
    (14, {
        'kicker': "Working",
        'title':  "One suite, three sets of limits",
        'body': [
            p("Production, stress-chamber and aged-equipment tolerances are three tables"),
            p("A limit unchanged from production is written once and referenced — it cannot drift"),
            p("All three are compiled in; the operator picks per run"),
            p(""),
            *code("run_scripts --criteria=stress"),
            p(""),
            note("An unknown name is fatal and lists the ones that would have worked.", accent=True),
        ],
    }),

    # 12 ------------------------------------------------- running it
    (15, {
        'kicker': "Working",
        'title':  "Running what you just wrote",
        'left': [
            richnote(("Pick from a menu", False, True)),
            p(""),
            *code("tools/run-tests.sh"),
            p(""),
            p("Group, then one, several or all of its tests"),
            p("The catalog only — never build artefacts"),
        ],
        'right': [
            richnote(("Or say exactly what you want", False, True)),
            p(""),
            p("--select=A,B  ·  --repeat=50"),
            p("--until-failure  ·  --list-tests"),
            p("--record=  ·  --replay="),
            p("--safe drops the rig to idle and exits"),
            p(""),
            note("Exit code is 0 only if every selected test passed.", accent=True),
        ],
    }),

    # 13 ------------------------------------------------- the logs
    (15, {
        'kicker': "Evidence",
        'title':  "One event stream, two audiences",
        'left': [
            richnote(("For people", False, True)),
            p(""),
            p("Colour-coded RTF, opens in Word"),
            p("Grouped the way your catalog is"),
            p("Each check states what was measured and what was required"),
            p("Readable while the run is still going"),
        ],
        'right': [
            richnote(("For tools", False, True)),
            p(""),
            p("SARIF 2.1.0 — an industry format"),
            p("Every step, including routing and safing"),
            p("Each requirement becomes a queryable rule"),
            p("Ingested by CI with no custom parser"),
        ],
    }),

    # 14 ------------------------------------------------- traceability
    (14, {
        'kicker': "Evidence",
        'title':  "Both logs answer: which run was this?",
        'body': [
            p("Device and serial number  ·  operator  ·  which tolerance table"),
            p("Framework version  ·  revision of the scripts, device and rig data"),
            p("The command line  ·  local time for a person, UTC for a machine"),
            p(""),
            note("Stamped in by the build, not typed in by a caller — so a report cannot claim the wrong device or the wrong limits.", accent=True),
        ],
    }),

    # 15 ================================================= ACT 2
    (6, {
        'kicker': "Act 2",
        'title':  "The mechanisms that make that possible",
    }),

    # 16 ------------------------------------------------- layering
    (14, {
        'kicker': "Architecture",
        'title':  "Framework, drivers, and one bench's data",
        'body': [
            rich(0, ("framework/  ", False, True), ("units, criteria, the verbs, the journal, the runner — portable", False, False)),
            rich(0, ("instruments/  ", False, True), ("one directory per driver, each packageable on its own", False, False)),
            rich(0, ("rig/ dut/ suite/  ", False, True), ("this bench, this device, these tests — data, not code", False, False)),
            p(""),
            note("A second bench testing a second device is its own repository.", accent=True),
            note("It reuses framework/ untouched and brings its own three."),
        ],
    }),

    # 17 ------------------------------------------------- routes
    (14, {
        'kicker': "Mechanism",
        'title':  "A route is composed, never stored",
        'body': [
            p("An instrument's channel is fixed, whichever pin you measure"),
            p("A connector pin's channel is fixed, whichever instrument measures it"),
            p("Two independent tables — not one keyed by every (instrument, pin) pair"),
            p(""),
            richnote(("A third table names what is not routed at all: ", False, False),
                     ("which pin each hard-cabled supply lands on, so nobody invents a relay path to a live rail.", False, True)),
        ],
    }),

    # 18 ------------------------------------------------- types
    (15, {
        'kicker': "Mechanism",
        'title':  "The point is its type",
        'left': [
            *code("POINT( Output5V,",
                  "       A, 1, 3, ...)",
                  "",
                  "// becomes",
                  "",
                  "AdapterPointTag<",
                  "  VpcLocation{ A, 1, 3 }>"),
        ],
        'right': [
            p("A test point's location is a template argument, not runtime data"),
            p("So a point is a struct member — and a typo is “no such member”"),
            p("No name lookup at run time: the point is the lookup result"),
            p(""),
            note("The same trick gives criteria, scope channels and units their errors.", accent=True),
        ],
    }),

    # 19 ------------------------------------------------- reflection
    (14, {
        'kicker': "Mechanism",
        'title':  "Derive it, do not assert it",
        'body': [
            p("C++26 reflection walks the DUT's own declarations at compile time"),
            p("Every declared point must have a wiring entry — checked by looking, not by a list"),
            p("Enum names, criteria groups and variant tables come from the declarations themselves"),
            p(""),
            note("A static_assert that catches a forgotten edit is worse than a design with nothing to forget.", accent=True),
        ],
    }),

    # 20 ------------------------------------------------- compile-only tests
    (14, {
        'kicker': "Mechanism",
        'title':  "Three test files with no assertions in them",
        'body': [
            p("Their entire purpose is to compile — reaching the end means the checks held"),
            p("Wiring coverage, criteria-variant parity, adapter consistency"),
            p("Whatever they prove, they prove for every declaration, not the ones a run happened to reach"),
            p(""),
            note("402 tests in all, 45 of them driving the real binary as a subprocess.", accent=True),
        ],
    }),

    # 21 ------------------------------------------------- honest limits
    (14, {
        'kicker': "Mechanism",
        'title':  "What is deliberately still runtime",
        'body': [
            p("Whether this rig wired a given pin at all"),
            p("Whether an instrument answers, and what it answers"),
            p(""),
            richnote(("A script's translation unit never sees the rig's wiring table, on purpose — and constant evaluation cannot reach across translation units the way linking can. ", False, False),
                     ("So the check runs where both are visible, and fails your build rather than your run.", False, True)),
        ],
    }),

    # 22 ================================================= honesty
    (6, {
        'kicker': "Honestly",
        'title':  "What this buys, and what it costs",
    }),

    # 23 ------------------------------------------------- pros / cons
    (15, {
        'kicker': "Honestly",
        'title':  "Pros and cons",
        'left': [
            richnote(("Strengths", False, True)),
            p(""),
            p("Whole classes of mistake cannot reach the bench"),
            p("Scripts trace straight to the spec"),
            p("Operator view and machine record cannot disagree"),
            p("Scripts testable with no hardware"),
            p("One tolerance value, three tables"),
            p("Reusable across benches"),
        ],
        'right': [
            richnote(("Costs and risks", False, True)),
            p(""),
            p("Experimental C++26; GCC 16 only"),
            p("New scripts need a developer, not a spreadsheet"),
            p("Wiring changes mean a rebuild"),
            p("Few engineers have used reflection"),
            p("Instrument drivers still simulated"),
            p("Template errors can be verbose"),
        ],
    }),

    # 24 ------------------------------------------------- status
    (14, {
        'kicker': "Honestly",
        'title':  "Done, and open",
        'body': [
            richnote(("Working today. ", False, True), ("Units and tolerances, routed measurements, the switching fabric, safing, three tolerance variants, injection and replay, both log streams, four drivers, the catalog runner and its CLI.", False, False)),
            p(""),
            richnote(("Next. ", False, True), ("A real instrument transport, so the drivers talk to hardware instead of a simulation. Then per-phase angle on the AC source, and deciding whether a second bench is a fork or a package.", False, False)),
        ],
    }),

    # 25 ------------------------------------------------- conclusion
    (17, {
        # NB: on the Conclusion layout the "title" placeholder is a 12pt label in
        # the top corner -- the headline goes in the content area, whose first
        # paragraph the layout already sets at 32pt.
        'title': "In closing",
        'body': [
            note("The rig should never find your mistake first."),
            note("", lvl=1),
            note("You write the test and the tolerance. Everything that can be "
                 "decided before power-on already has been.", lvl=1),
        ],
    }),
]
