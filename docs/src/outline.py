# -*- coding: utf-8 -*-
# The deck as content: (layout number, {placeholder-key: content}).
# Placeholder keys: 'kicker' (small label above title), 'title', 'body',
# 'left', 'right', 'sub' (title-slide subtitle), 'quote', 'attrib'.
# Content is a list of (level, [(text, bold), ...]) paragraphs, or a plain string.

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
        'sub':   "Hardware test automation that fails at compile time\nThomas Rambrant  ·  Knowit Connectivity",
    }),

    # 2 -------------------------------------------------- divider
    (6, {
        'kicker': "The problem",
        'title':  "A test rig fails in expensive ways",
    }),

    # 3 -------------------------------------------------- content
    (14, {
        'kicker': "The problem",
        'title':  "Four ways a test suite lies to you",
        'body': [
            p("A mistyped test point measures the wrong pin — and passes"),
            p("A criterion compared against the wrong unit — and passes"),
            p("A relay path nobody wired up — throws three hours into a soak test"),
            p("A tolerance table edited for one variant, not the others"),
            p(""),
            note("Every one of these is a static fact.", accent=True),
            note("None of them needs a running rig to discover."),
        ],
    }),

    # 4 -------------------------------------------------- the rule
    (19, {
        'quote':  "If a mistake can be caught by the compiler,\nit must be caught by the compiler.",
        'attrib': "Runtime checks are a last resort — and each one is a documented concession.",
    }),

    # 5 -------------------------------------------------- divider
    (6, {
        'kicker': "How it works",
        'title':  "A test script reads like the test specification",
    }),

    # 6 -------------------------------------------------- code + what's hidden
    (15, {
        'kicker': "How it works",
        'title':  "What a script never mentions",
        'left': [
            richnote(("The whole test", False, True)),
            p(""),
            *code("auto rail = Measure(",
                  "    Dmm1.voltage(),",
                  "    at( DeviceX::Output5V));",
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
            p("Tolerance value"),
            p("A single logging call"),
            p(""),
            note("All resolved elsewhere — most of it by the compiler.", accent=True),
        ],
    }),

    # 7 -------------------------------------------------- the layers
    (14, {
        'kicker': "How it works",
        'title':  "Four sources of truth",
        'body': [
            rich(0, ("rig/  ", False, True), ("which instruments this bench has, and how they are wired", False, False)),
            rich(0, ("dut/  ", False, True), ("named test points and tolerance tables", False, False)),
            rich(0, ("suite/  ", False, True), ("the test scripts and the catalog that lists them", False, False)),
            rich(0, ("libs/  ", False, True), ("the portable framework — knows nothing about either", False, False)),
            p(""),
            note("A second bench testing a second device is its own repository.", accent=True),
            note("It reuses libs/ untouched and brings its own three."),
        ],
    }),

    # 8 -------------------------------------------------- divider
    (6, {
        'kicker': "The payoff",
        'title':  "Fourteen mistakes that now fail the build",
    }),

    # 9 -------------------------------------------------- the list
    (15, {
        'kicker': "The payoff",
        'title':  "What the compiler now refuses",
        'left': [
            p("A misspelled test point"),
            p("A misspelled criterion"),
            p("A criterion in the wrong unit"),
            p("A test point nobody wired up"),
            p("A tolerance variant missing a limit"),
        ],
        'right': [
            p("A renamed script in the catalog"),
            p("Connecting a supply with no relay"),
            p("Channel 5 on a 4-channel scope"),
            p("A driver with no safe state"),
            p("A half-declared unit"),
            p("…and four more"),
            p(""),
            note("Seconds, on a laptop — not hours, on the bench.", accent=True),
        ],
    }),

    # 10 ------------------------------------------------- the clever two
    (14, {
        'kicker': "The payoff",
        'title':  "Two checks worth a closer look",
        'body': [
            richnote(("Wiring coverage. ", False, True), ("One test file has no assertions at all — the point is that it compiles. It walks every declared test point and demands a matching wiring entry. Forget one and the build stops.", False, False)),
            p(""),
            richnote(("Tolerance variants. ", False, True), ("Production, stress-chamber and aged-equipment limits are three tables. A value unchanged from production is written once and referenced — so it cannot drift, and a typo in the reference will not compile.", False, False)),
        ],
    }),

    # 11 ------------------------------------------------- divider
    (6, {
        'kicker': "Evidence",
        'title':  "Every run explains itself, twice",
    }),

    # 12 ------------------------------------------------- the two logs
    (15, {
        'kicker': "Evidence",
        'title':  "One event stream, two audiences",
        'left': [
            richnote(("For people", False, True)),
            p(""),
            p("Colour-coded RTF, opens in Word"),
            p("Grouped the way the catalog is"),
            p("Each check states what was measured and what was required"),
            p("Readable while the run is going"),
            p("Live on the console too"),
        ],
        'right': [
            richnote(("For tools", False, True)),
            p(""),
            p("SARIF 2.1.0 — an industry format"),
            p("Every step, routing and safing"),
            p("Each requirement becomes a queryable rule"),
            p("Ingested by CI, no custom parser"),
        ],
    }),

    # 13 ------------------------------------------------- traceability
    (14, {
        'kicker': "Evidence",
        'title':  "Both logs answer: which run?",
        'body': [
            p("Device and serial number  ·  operator  ·  tolerance table"),
            p("Framework version  ·  revision of the scripts, device and rig data"),
            p("The command line  ·  local time for a person, UTC for a machine"),
            p(""),
            note("Stamped in by the build, not typed in by a caller — so a report cannot claim the wrong device or the wrong limits.", accent=True),
        ],
    }),

    # 14 ------------------------------------------------- testing
    (14, {
        'kicker': "Evidence",
        'title':  "278 tests, and none of them need a rig",
        'body': [
            p("Test scripts run with readings injected, or replayed from a real run"),
            p("Two test targets exist only to fail the build on inconsistent data"),
            p("Seventeen acceptance tests drive the real binary as a subprocess and keep every log they produce"),
            p(""),
            note("A developer can change the framework and know within seconds whether a bench would still behave.", accent=True),
        ],
    }),

    # 15 ------------------------------------------------- divider
    (6, {
        'kicker': "Honestly",
        'title':  "What this buys, and what it costs",
    }),

    # 16 ------------------------------------------------- pros / cons
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

    # 17 ------------------------------------------------- status
    (14, {
        'kicker': "Honestly",
        'title':  "Done, and open",
        'body': [
            richnote(("Working today. ", False, True), ("Units and tolerances, routed measurements, a switching fabric, safing, three tolerance variants, session injection and replay, both log streams, the catalog runner and its CLI.", False, False)),
            p(""),
            richnote(("Next. ", False, True), ("A real instrument transport. Moving the remaining runtime wiring check to compile time. Deciding whether a second bench is a fork or a package.", False, False)),
        ],
    }),

    # 18 ------------------------------------------------- conclusion
    (17, {
        # NB: on the Conclusion layout the "title" placeholder is a 12pt label in
        # the top corner -- the headline goes in the content area, whose first
        # paragraph the layout already sets at 32pt.
        'title': "In closing",
        'body': [
            note("The rig should never find your mistake first."),
            note("", lvl=1),
            note("The compiler is cheaper, faster and more thorough than a bench — "
                 "so everything that can be decided before power-on, is.", lvl=1),
        ],
    }),
]
