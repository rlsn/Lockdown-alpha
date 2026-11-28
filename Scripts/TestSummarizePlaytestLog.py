import contextlib
import io
import json
import re
import tempfile
import unittest
from pathlib import Path

from SummarizePlaytestLog import (baseline_at, baseline_from_runs, check_client, collect, main, parse_baseline, parse_line,
                                  summarize)

HOST = "0000000000000000000000000000000A"
CLIENT = "0000000000000000000000000000000B"
AI = "0000000000000000000000000000000C"
CAMP = "0000000000000000000000000000000D"


def stamp(seconds):
    whole = int(seconds)
    millis = int(round((seconds - whole) * 1000))
    return f"[2026.11.06-03.{10 + whole // 60:02d}.{whole % 60:02d}:{millis:03d}][  1]"


def line(seconds, category, verbosity, message):
    level = "" if verbosity == "Log" else f"{verbosity}: "
    return f"{stamp(seconds)}{category}: {level}{message}"


HOST_LOG = [
    "LogInit: Display: Engine started without a timestamp",
    line(0, "LogLDMatch", "Display", "LD_DEMO_LOBBY generation=7"),
    line(1, "LogLDMatch", "Warning", "LD_DEMO_ADMISSION_REJECTED lobby noise before the match"),
    line(9.9, "LogLDMatch", "Display", "LD_MATCH_STARTING generation=7 ai=1"),
    line(9.9, "LogLDGrowth", "Log", f"LD_OFFER t=0 contender={AI} ordinal=1 level=1 cards=active_crotch_grab"),
    line(10, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=7 contenders=3 open_at_ms=480000"),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_HEADER generation=7 ledger=F product=0.10.0 wire=6 commit={'a' * 40} dirty=0 "
         f"catalog=abcdef0123456789 net=ListenServer address=\"192.168.1.2:7777\" contenders=3 ai=1 open_at_ms=480000 dev_test=0 dev_reload=0"),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=7 slot=0 contender={HOST} name=\u623f\u4e3b kind=Human host=1 character=c address=local"),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=7 slot=1 contender={CLIENT} name=\"Little \\\"Wang\\\"\" kind=Human host=0 character=c address=\"127.0.0.1:5555\""),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=7 slot=2 contender={AI} name=\"AI 1\" kind=AI host=0 character=c address=ai"),
    line(11, "LogLDTrace", "Log", f"LD_HEARTBEAT t=1000 contender={CLIENT} phase=Alive life=1 pos=1,2,3 hp=500.00/500.00 momentum=50.00/100.00 shield=0.00 level=1 xp=0 pending=1 order=none order_at=- action=- statuses=- protected_until=0 respawn_at=0"),
    line(12, "LogLDGrowth", "Log", f"LD_OFFER t=2000 contender={CLIENT} ordinal=1 level=1 cards=active_snark_bolt,active_pants_down,active_seethe"),
    line(13, "LogLDGrowth", "Log", f"LD_CHOICE t=3000 contender={CLIENT} ordinal=1 kind=ActiveChoice card=active_snark_bolt result=learned slot=Q stored=None level=1 pending=0"),
    line(20, "LogLDCombat", "Log", f"LD_HIT t=10000 src={AI} src_kind=contender src_monster=- credit={AI} victim={CLIENT} victim_life=1 victim_kind=contender victim_monster=- via=basic class=basic type=physical dmg=60.00 pre=60.00 absorbed=10.00 shattered=0.00 hp_lost=50.00 hp=500.00->450.00 max_hp=500.00 shield=10.00->0.00 amp_bp=10000 resist=10.00 mitigate_bp=0 crit=0 backstab=0 protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal=0"),
    line(25, "LogLDCombat", "Log", f"LD_HIT t=15000 src={AI} src_kind=contender src_monster=- credit={AI} victim={CLIENT} victim_life=1 victim_kind=contender victim_monster=- via=learned:active_crotch_grab class=ability type=physical dmg=450.00 pre=450.00 absorbed=0.00 shattered=0.00 hp_lost=450.00 hp=450.00->0.00 max_hp=500.00 shield=0.00->0.00 amp_bp=10000 resist=10.00 mitigate_bp=0 crit=1 backstab=0 protected=0 periodic=0 reflect=0 added=0 controls=stun vamp=0.00 lethal=1"),
    line(26, "LogLDCombat", "Log", f"LD_HIT t=16000 src={HOST} src_kind=contender src_monster=- credit={HOST} victim={CAMP} victim_life=1 victim_kind=camp victim_monster=monster_scavenger via=basic class=basic type=physical dmg=5.00 pre=5.00 absorbed=0.00 shattered=0.00 hp_lost=5.00 hp=80.00->75.00 max_hp=80.00 shield=0.00->0.00 amp_bp=10000 resist=0.00 mitigate_bp=0 crit=0 backstab=0 protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal=0"),
    line(25.5, "LogLDMatch", "Display", f"LD_DEATH_RECEIPT contender={CLIENT} life=1 respawn_at_ms=21000 t=15000 killer={AI} cause=Contender fatal=learned:active_crotch_grab fatal_body={AI} assisters={HOST} no_reward=None victim_level=1"),
    line(25.5, "LogLDGrowth", "Log", f"LD_REWARD t=15000 contender={AI} cause=death role=killer xp=80 xp_after=80 level=1->2 victim={CLIENT} victim_life=1 victim_kind=contender"),
    line(25.5, "LogLDGrowth", "Log", f"LD_LEVEL_UP t=15000 contender={AI} level=1->2 choices_earned=1 unspent_points=6"),
    line(31, "LogLDMatch", "Log", f"LD_CAMP_DEATH member={CAMP} life=1 monster=monster_scavenger t=21000 killer={HOST} fatal=basic no_reward=None"),
    line(31, "LogLDGrowth", "Log", f"LD_REWARD t=21000 contender={HOST} cause=death role=killer xp=40 xp_after=40 level=1->1 victim={CAMP} victim_life=1 victim_kind=camp"),
    line(31.5, "LogLDNav", "Display", "LD_NAV_ACCEPT match=7 life=1 order=3 epoch=0 kind=0 goal=X=0"),
    line(32, "LogLDMatch", "Display", f"LD_PLAYER_MARK source=host_view contender={CLIENT} name=\"Little \\\"Wang\\\"\" seq=1 t=22000 client_t=21950 phase=Dead pos=-"),
    line(40, "LogLDNet", "Log", "LD_PERF role=host window_s=10.0 fps=60.0 frame_ms=16.67 peak_ms=40.0 frames=600 hitches=0 hitch_ms=100"),
    line(40, "LogLDNet", "Log", f"LD_NET role=host contender={CLIENT} address=\"127.0.0.1:5555\" rtt_ms=40 jitter_ms=3.5 loss_in=1.0 loss_out=0.0 since_recv_s=0.01 corrections_sent=2"),
    line(41, "LogLDNet", "Log", "LD_PERF_HITCH role=host frame_ms=180.5 t=31000 suppressed=2"),
    line(42, "LogLDCombat", "Error", "LD_DEMO_AUTHORITY_FAULT something broke"),
    line(50, "LogLDMatch", "Display", "LD_MATCH_RESULT generation=7 reason=1 winner=X ended_ms=40000"),
    line(50, "LogLDMatch", "Display", f"LD_MATCH_SUMMARY generation=7 reason=GuardSucceeded winner={AI} started_ms=0 ended_ms=40000 duration_ms=40000"),
    line(50, "LogLDMatch", "Display", f"LD_MATCH_SUMMARY_ROW generation=7 contender={AI} name=\"AI 1\" kind=AI left=0 winner=1 level=2 xp=80 kills=1 deaths=0 assists=0 choices=1 skills=active_crotch_grab/Q blueprints=- points=3,0,0,0,0"),
    line(55, "LogLDMatch", "Display", "LD_ROOM_RESET generation=8 aborted_start=0"),
    line(60, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=8 contenders=2 open_at_ms=480000"),
]

CLIENT_LOG = [
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_MATCH match=7 contender={CLIENT} name=\"Little \\\"Wang\\\"\" product=0.10.0 wire=6 commit={'a' * 40} server=\"192.168.1.2:7777\""),
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_ROSTER match=7 slot=0 contender={HOST} name=\u623f\u4e3b kind=Human host=1 self=0"),
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_ROSTER match=7 slot=1 contender={CLIENT} name=\"Little \\\"Wang\\\"\" kind=Human host=0 self=1"),
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_ROSTER match=7 slot=2 contender={AI} name=\"AI 1\" kind=AI host=0 self=0"),
    line(11.2, "LogLDTrace", "Log", "LD_SELF_STATE t=1000 match=7 life=1 phase=Alive pos=1,2,3 hp=500.00/500.00 momentum=50.00/100.00 shield=0.00 level=1 xp=0 pending=1 order=none action=- statuses=-"),
    line(12.2, "LogLDGrowth", "Log", "LD_SELF_OFFER t=2000 ordinal=1 level=1 cards=active_snark_bolt,active_pants_down,active_seethe"),
    line(20.2, "LogLDCombat", "Log", "LD_SELF_HP t=10000 kind=shield_absorbed amount=10 hp=450.00/500.00 shield=0.00"),
    line(20.2, "LogLDCombat", "Log", "LD_SELF_HP t=10000 kind=damage amount=50 hp=450.00/500.00 shield=0.00"),
    line(24.0, "LogLDCombat", "Log", f"LD_SELF_SEEN_CAST t=14000 cast=\"Lunge\" unit={AI} pos=5,5,0"),
    line(25.2, "LogLDCombat", "Log", "LD_SELF_STATUS t=15000 added=stun:500 removed=- controlled=1"),
    line(25.7, "LogLDMatch", "Display", f"LD_SELF_KILLFEED serial=1 t=15000 victim={CLIENT} victim_name=\"Little \\\"Wang\\\"\" killer={AI} killer_name=\"AI 1\" cause=Contender assisters={HOST}"),
    line(25.7, "LogLDMatch", "Display", f"LD_SELF_DEATH t=15000 killer={AI} killer_name=\"AI 1\" cause=Contender assisters={HOST}"),
    line(32.0, "LogLDMatch", "Display", f"LD_PLAYER_MARK source=self seq=1 contender={CLIENT} t=21950 match=7 life=1 phase=Dead pos=- hp=- momentum=- shield=- level=1 xp=0 pending=0 order=none action=- statuses=-"),
    line(40.2, "LogLDNet", "Log", "LD_NET role=client server=\"192.168.1.2:7777\" rtt_ms=41 jitter_ms=2.0 loss_in=0.0 loss_out=0.5 since_recv_s=0.02 corrections=3 last_correction_cm=4.0 body_max_correction_cm=9.0"),
    line(50.2, "LogLDMatch", "Display", f"LD_SELF_RESULT match=7 reason=GuardSucceeded winner={AI} winner_name=\"AI 1\" duration_ms=40000"),
]


class SummarizePlaytestLogTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "Host.log").write_text("\n".join(HOST_LOG) + "\n", encoding="utf-8-sig")
        (self.root / "Client1.log").write_text("\n".join(CLIENT_LOG) + "\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_parse_line_reads_ue_prefix_fields_and_quotes(self):
        event = parse_line(line(3.25, "LogLDMatch", "Display", 'LD_X a=1 name="A \\"b\\" c" empty="" tail=x=y'), "Host")
        self.assertEqual((event.category, event.verbosity, event.tag), ("LogLDMatch", "Display", "LD_X"))
        self.assertEqual(event.fields, {"a": "1", "name": 'A "b" c', "empty": "", "tail": "x=y"})
        self.assertEqual(event.when.microsecond, 250000)
        self.assertEqual(parse_line(line(0, "LogLDCombat", "Log", "LD_HIT t=1").strip(), "Host").verbosity, "Log")
        self.assertIsNone(parse_line("LogInit: no timestamp", "Host"))
        self.assertEqual(parse_line(line(0, "LogNet", "Warning", "plain engine text"), "Host").tag, "")

    def test_host_log_alone_gives_roster_deaths_growth_marks_and_network(self):
        text = summarize(collect([str(self.root / "Host.log")]))
        self.assertIn("## Match 7", text)
        self.assertIn("## Match 8", text)
        self.assertIn("- 1: Little \"Wang\" [Human]", text)
        self.assertIn("- 0: \u623f\u4e3b [Human (host)]", text)
        self.assertIn("Little \"Wang\" died to AI 1 via learned:active_crotch_grab (assists \u623f\u4e3b; no_reward None)", text)
        # Both hits on the victim inside the death window, the unrelated camp hit is not listed under the death.
        deaths = text.split("### Deaths")[1].split("###")[0]
        self.assertIn("AI 1 -> Little \"Wang\" basic basic dmg 60.00 (shield 10.00) hp 500.00->450.00", deaths)
        self.assertIn("AI 1 -> Little \"Wang\" learned:active_crotch_grab ability dmg 450.00", deaths)
        self.assertIn("[crit, stun]", deaths)
        self.assertNotIn("camp:monster_scavenger", deaths)
        growth = text.split("### Growth")[1].split("###")[0]
        for expected in ("offer #1", "chose active_snark_bolt (learned, slot Q, stored None)",
                         "+80 xp (death killer of Little \"Wang\") xp 80 level 1->2",
                         "AI 1: level 1->2 (+1 active offers)",
                         "00:00.0 AI 1: offer #1: active_crotch_grab",
                         "+40 xp (death killer of camp:monster_scavenger) xp 40 level 1->1"):
            self.assertIn(expected, growth)
        self.assertIn("AI 1: K/D/A 1/0/0 level 2 skills active_crotch_grab/Q blueprints - points 3,0,0,0,0", text)
        marks = text.split("### Marks")[1].split("###")[0]
        self.assertIn("mark #1 by Little \"Wang\" [Host, host_view] Dead", marks)
        self.assertIn("state 00:01.1", marks)
        self.assertIn("LD_DEATH_RECEIPT", marks)
        self.assertNotIn("LD_NAV_ACCEPT", marks)
        self.assertIn("(+1 routine AI/order lines; --mark-all lists them)", marks)
        self.assertIn("LD_NAV_ACCEPT", summarize(collect([str(self.root / "Host.log")]), mark_all=True).split("### Marks")[1])
        self.assertNotIn("LD_HEARTBEAT", marks.split("state")[1].split("\n", 1)[1])
        network = text.split("### Performance and network")[1].split("###")[0]
        self.assertIn("Host (host): 1 windows, fps min 60.0", network)
        self.assertIn("Host -> Little \"Wang\": rtt avg 40 max 40 ms", network)
        self.assertIn("hitch 180.5 ms on Host", network)
        problems = text.split("### Warnings and errors")[1]
        self.assertIn("Error x1 [Host]", problems)
        self.assertIn("## Outside a match", text)
        self.assertIn("LD_DEMO_ADMISSION_REJECTED", text.split("## Outside a match")[1])
        self.assertIn("### Result", text)
        self.assertIn("GuardSucceeded winner AI 1 duration 40.0s", text)

    def test_client_log_alone_gives_its_own_view(self):
        text = summarize(collect([str(self.root / "Client1.log")]))
        self.assertIn("## Match 7", text)
        self.assertIn("Client1: joined as Little \"Wang\"", text)
        self.assertIn("[Client1] own death: killed by AI 1 (Contender)", text)
        self.assertIn("Little \"Wang\" died to AI 1 (Contender, assists \u623f\u4e3b) [public kill feed, Client1]", text)
        deaths = text.split("### Deaths")[1].split("###")[0]
        self.assertIn("kind=damage amount=50", deaths)
        self.assertIn("added=stun:500", deaths)
        self.assertIn("unit=AI 1", deaths)
        self.assertIn("offer #1", text.split("### Growth")[1])
        self.assertIn("mark #1 by Little \"Wang\" [Client1, self]", text)
        self.assertIn("Client1 -> server: rtt avg 41", text)

    def test_merged_logs_are_ordered_by_utc_across_files(self):
        text = summarize(collect([f"Host={self.root / 'Host.log'}", f"Me={self.root / 'Client1.log'}"]))
        marks = text.split("### Marks")[1].split("###")[0]
        self.assertIn("[Host, host_view]", marks)
        self.assertIn("[Me, self]", marks)
        deaths = text.split("### Deaths")[1].split("###")[0]
        host_death = deaths.index("died to AI 1")
        own_death = deaths.index("[Me] own death")
        self.assertLess(host_death, own_death)
        self.assertNotIn("public kill feed", deaths, "the Host's death record replaces the client's copy of the feed")
        self.assertIn("sources: Host, Me", text)

    def test_directory_input_and_client_privacy_check(self):
        logs = dict(collect([str(self.root)]))
        self.assertEqual(sorted(logs), ["Client1", "Host"])
        self.assertEqual(check_client(logs["Client1"]), [])
        self.assertEqual(check_client(logs["Host"]), [], "authority lines while hosting are the Host's own record")
        lobby_only = [line(0, "LogLDMatch", "Display", "LD_DEMO_LOBBY generation=1"),
                      line(10, "LogLDNet", "Log", f"LD_NET role=host contender={CLIENT} address=x rtt_ms=1 corrections_sent=0"),
                      line(11, "LogLDMatch", "Display", f"LD_MEMBER_LEFT contender={CLIENT} name=x generation=1 phase=Lobby t=0 host=0")]
        self.assertEqual(check_client([parse_line(row, "Lobby") for row in lobby_only]), [])
        hosted_then_joined = lobby_only + [line(20, "LogLDMatch", "Display", "LD_DEMO_FRONTEND"),
                                           line(30, "LogLDCombat", "Log", f"LD_HIT t=1 src={AI} victim={HOST}")]
        leaks = check_client([parse_line(row, "Mixed") for row in hosted_then_joined])
        self.assertEqual([event.tag for event in leaks], ["LD_HIT"])
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main([str(self.root), "--check-client", "--out", str(self.root / "report.md")]), 0)
        self.assertIn("## Match 7", (self.root / "report.md").read_text(encoding="utf-8"))
        leaked = CLIENT_LOG + [line(30, "LogLDCombat", "Log", f"LD_HIT t=1 src={AI} victim={HOST}")]
        (self.root / "Client1.log").write_text("\n".join(leaked) + "\n", encoding="utf-8")
        errors = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            self.assertEqual(main([str(self.root / "Client1.log"), "--check-client"]), 1)
        self.assertIn("Host-only line while not hosting", errors.getvalue())

    def test_quoted_unicode_spaces_stay_one_value(self):
        event = parse_line(line(0, "LogLDMatch", "Display", 'LD_X name="\u738b\u3000\u5c0f" next=1'), "Host")
        self.assertEqual(event.fields, {"name": "\u738b\u3000\u5c0f", "next": "1"})

    def test_shared_display_names_are_told_apart(self):
        rows = [
            line(10, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=3 contenders=2 open_at_ms=1"),
            line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=3 slot=0 contender={HOST} name=\u73a9\u5bb6 kind=Human host=1 character=c address=local"),
            line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=3 slot=1 contender={CLIENT} name=\u73a9\u5bb6 kind=Human host=0 character=c address=x"),
            line(12, "LogLDMatch", "Display", f"LD_DEATH_RECEIPT contender={CLIENT} life=1 respawn_at_ms=1 t=1 killer={HOST} cause=Contender fatal=basic fatal_body={HOST} assisters=- no_reward=None victim_level=1"),
        ]
        (self.root / "Same.log").write_text("\n".join(rows) + "\n", encoding="utf-8")
        text = summarize(collect([str(self.root / "Same.log")]))
        self.assertIn("\u73a9\u5bb6(000B) died to \u73a9\u5bb6(000A)", text)
        self.assertIn("- 0: \u73a9\u5bb6(000A) [Human (host)]", text)

    def test_no_match_is_reported_explicitly(self):
        (self.root / "Empty.log").write_text(line(0, "LogLDMatch", "Display", "LD_DEMO_LOBBY generation=1") + "\n", encoding="utf-8")
        text = summarize(collect([str(self.root / "Empty.log")]))
        self.assertIn("No match was found", text)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main([str(self.root / "missing-dir")]), 2)


    def test_metrics_from_pre_p2_lines_say_no_data_where_lines_are_missing(self):
        text = summarize(collect([str(self.root / "Host.log")]))
        metrics = text.split("## Match 7")[1].split("### Metrics (spec §16)")[1].split("###")[0]
        # Hits on contenders: the basic 60 and the skill 450; on camps the basic 5. Old LD_HIT carries amp_bp, no factors.
        self.assertIn("hits on contenders: 510 dmg: basic 11.8%, skill 88.2%, card 0.0%, other 0.0%", metrics)
        self.assertIn("reaction share: no data (LD_HIT has no reaction field)", metrics)
        self.assertIn("hits on camps: 5 dmg: basic 100.0%", metrics)
        self.assertIn("Casts per person per fight (Q/W/E releases): no data (no LD_CAST line", metrics)
        # The victim never hit back: a one-sided kill is counted but is no time to kill of a fight.
        self.assertIn("Time to kill (first damage between the pair to the death): no data (+1 one-sided kills)", metrics)
        self.assertIn("same level L1: no data (+1 one-sided kills)", metrics)
        self.assertIn("Momentum income: no data (no LD_MOMENTUM line)", metrics)
        self.assertIn("Outrageous hits (>= 60% of the target's max HP): 1", metrics)
        self.assertIn("learned:active_crotch_grab 450.00 (90% of 500.00)", metrics)
        self.assertIn("Longest chain: no data (no LD_CHAIN line)", metrics)
        self.assertIn("Damage factors: no data (LD_HIT has no factor_bp; old amp_bp peaks at 10000)", metrics)
        self.assertIn("Cards offered: no data (no card offer)", metrics)
        across = text.split("## Metrics across 1 matches with Host lines")[1]
        self.assertIn("| Same-level L1 time to kill | 10–12 s | no data (+1 one-sided kills) |", across)
        self.assertIn("| Casts per person per same-level fight | 7–10 | no data (no LD_CAST line", across)
        # The old summary row has no cards field: winner builds are not comparable.
        self.assertIn("| Distinct winner builds (actives, points, installed cards) | ≥ 8 per 50 matches | "
                      "no data (no winner row with points and cards) |", across)
        self.assertIn("Actives pick rate (picked / offered), 4 ids, 3 below 4%:", across)
        self.assertIn("| active_snark_bolt | 1 | 1 | 100.0% |", across)
        self.assertIn("Cards pick rate: no data (none offered)", across)
        client = summarize(collect([str(self.root / "Client1.log")]))
        self.assertIn("no data: the metrics need the Host log", client)
        self.assertIn("## Metrics across 0 matches with Host lines", client)

    def test_metrics_read_p2_lines_casts_factors_reactions_and_chains(self):
        a, b = AI, CLIENT

        def hit(at, t, src, victim, via, dmg, extra="factor_bp=10000 factors=-"):
            return line(at, "LogLDCombat", "Log",
                        f"LD_HIT t={t} src={src} src_kind=contender src_monster=- credit={src} victim={victim} victim_life=1 "
                        f"victim_kind=contender victim_monster=- via={via} class=ability type=magical dmg={dmg} absorbed=0.00 "
                        f"hp_lost={dmg} hp=860.00->1.00 max_hp=860.00 shield=0.00->0.00 resist=18.00 crit=0 protected=0 "
                        f"periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal=0 {extra}")

        rows = [
            line(1, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=4 contenders=2 started_at_ms=0"),
            line(1, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=4 slot=0 contender={a} name=A kind=AI host=0 character=c address=ai"),
            line(1, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=4 slot=1 contender={b} name=B kind=AI host=0 character=c address=ai"),
            line(2, "LogLDGrowth", "Log", f"LD_OFFER t=1000 contender={a} ordinal=1 level=1 cards=card_a01,card_k06,card_y16"),
            line(2, "LogLDGrowth", "Log", f"LD_CHOICE t=1100 contender={a} ordinal=1 kind=CardChoice card=card_k06 result=installed"),
            line(10, "LogLDTrace", "Log", f"LD_CAST t=10000 contender={a} life=1 casts=1 added=1"),
            hit(10, 10100, a, b, "learned:active_zap", "100.00", "factor_bp=15000 factors=card_k06:5000"),
            line(11, "LogLDTrace", "Log", f"LD_CAST t=11000 contender={b} life=1 casts=1 added=1"),
            hit(11, 11000, b, a, "basic", "50.00"),
            line(12, "LogLDTrace", "Log", f"LD_CAST t=12000 contender={a} life=1 casts=3 added=2"),
            hit(12, 12000, a, b, "card:card_y16", "40.00"),
            # Commit order: the reaction resolves inside the triggering hit, whose LD_HIT follows, then the queued settle.
            line(12, "LogLDCombat", "Log", f"LD_REACTION t=12000 kind=Overload trigger={a} via=learned:active_zap primary={b} struck=1 r=60.00 fire_settles=0"),
            hit(12, 12000, a, b, "learned:active_zap", "60.00"),
            hit(12, 12000, a, b, "learned:active_zap", "60.00"),
            hit(13, 13000, a, b, "learned:active_zap", "600.00", "factor_bp=120000 factors=card_k06:5000,card_big_one:6000"),
            line(13, "LogLDMatch", "Display", f"LD_DEATH_RECEIPT contender={b} life=1 respawn_at_ms=1 t=13000 killer={a} cause=Contender fatal=learned:active_zap fatal_body={a} assisters=- no_reward=None victim_level=1"),
            # B's released zone keeps hitting after its death: still B's last fight, not a new one.
            hit(13, 13500, b, a, "learned:active_quick_meeting", "20.00"),
            line(13, "LogLDCombat", "Log", f"LD_MOMENTUM entity={b} life=1 basic=20.00 combat=10.00 skill=0.00 reaction=0.00 other=0.00 idle=-5.00 spent=30.00 end=60.00"),
            line(13, "LogLDCombat", "Log", f"LD_CHAIN owner={a} steps=6 key=card_k06>Overload>kill damage=700.00"),
            line(14, "LogLDCombat", "Log", f"LD_MOMENTUM entity={a} life=1 basic=0.00 combat=10.00 skill=45.00 reaction=10.00 other=0.00 idle=0.00 spent=100.00 end=0.00"),
            # A rest-room training dummy is a contender-kind body outside the roster: never a contender for the metrics.
            hit(15, 15000, a, HOST, "learned:active_zap", "700.00", "factor_bp=900000 factors=card_k06:5000"),
            # Rest-room training: A's rest life 0 (every heartbeat before the first field entry) and the dummy's own ledger.
            line(16, "LogLDMatch", "Display", f"LD_TRAINING_DUMMY_SPAWN owner={a} dummy={HOST} level=1 slot=0"),
            line(16, "LogLDMatch", "Display", "LD_FIELD_ENTER generation=4 segment=0 at_ms=5000 field_t_ms=0 final=0"),
            line(16, "LogLDTrace", "Log", f"LD_HEARTBEAT t=1000 contender={a} phase=Alive life=0 pos=0,0,0"),
            line(16, "LogLDTrace", "Log", f"LD_HEARTBEAT t=10000 contender={a} phase=Alive life=1 pos=0,0,0"),
            line(16, "LogLDCombat", "Log", f"LD_MOMENTUM entity={a} life=0 basic=500.00 combat=0.00 skill=0.00 reaction=0.00 other=0.00 idle=0.00 spent=0.00 end=0.00"),
            line(16, "LogLDCombat", "Log", f"LD_MOMENTUM entity={HOST} life=1 basic=0.00 combat=0.00 skill=0.00 reaction=0.00 other=0.00 idle=0.00 spent=0.00 end=50.00"),
            line(16, "LogLDCombat", "Log", f"LD_REACTION t=2000 kind=Melt trigger={a} via=learned:active_zap primary={HOST} struck=0 r=1.00 fire_settles=0"),
            # Field from 5000; a rest 12000-14000; the showdown from 17000 is not field time; the match ends at 20000.
            line(17, "LogLDMatch", "Display", "LD_REST_ENTER generation=4 rest=1 at_ms=12000 field_t_ms=7000"),
            line(17, "LogLDMatch", "Display", "LD_FIELD_ENTER generation=4 segment=1 at_ms=14000 field_t_ms=7000 final=0"),
            line(17, "LogLDMatch", "Display", "LD_FIELD_ENTER generation=4 segment=2 at_ms=17000 field_t_ms=10000 final=1"),
            line(18, "LogLDMatch", "Display", "LD_MATCH_RESULT generation=4 reason=1 winner=X ended_ms=20000"),
        ]
        (self.root / "P2.log").write_text("\n".join(rows) + "\n", encoding="utf-8")
        text = summarize(collect([str(self.root / "P2.log")]), metrics_only=True)
        self.assertNotIn("### Roster", text)
        self.assertNotIn("### Deaths", text)
        metrics = text.split("### Metrics (spec §16)")[1].split("## Metrics across")[0]
        # 930 on contenders: skill 100 + 60 + 60 + 600 + B's zone after its death 20 (the Overload settle stays under its
        # applier's skill), card 40, basic 50.
        self.assertIn("hits on contenders: 930 dmg: basic 5.4%, skill 90.3%, card 4.3%, other 0.0%", metrics)
        # A's fight holds its three releases; B's fight its one (it died at 13000; its zone's later hit stays in that fight).
        self.assertIn("Casts per person per fight (Q/W/E releases): n=2 mean 2.0 median 2.0 min 1.0 max 3.0", metrics)
        self.assertIn("Time to kill (first damage between the pair to the death): n=1 mean 2.9 s", metrics)
        self.assertIn("Field time share (field segments before the showdown / whole match): 50.0% (10 s of 20 s)", metrics)
        self.assertIn("Momentum income: 95 over 2 field lives (basic 21.1%, combat 21.1%, skill 47.4%, reaction 10.5%, "
                      "other 0.0%); idle drift -5, spent 130 (2 rest-training ledger lines left out)", metrics)
        # A Host without the auto-empower mechanic writes no LD_EMPOWER and no empower= field: not applicable, never 0.
        self.assertIn("- Auto-empower: n/a (the Host log has no such mechanic)", metrics)
        # Both field lives with a heartbeat closed their ledgers.
        self.assertNotIn("incomplete:", metrics)
        # Without a measured baseline the breakout metric says so instead of guessing.
        self.assertIn("- Breakout builds: no data (no measured same-level bare-build baseline", metrics)
        across = text.split("## Metrics across 1 matches with Host lines")[1]
        self.assertIn("| Breakout builds per match (x3 the same-level bare build) | 1–2 | no data (no measured", across)
        self.assertIn("Breakout baseline: none measured.", across)
        # A dealt 860 in 3.5 s of fight (10000..13500); an L1 bare build at 72 DPS would deal 252. B dealt 70 in 2.9 s.
        measured = summarize(collect([str(self.root / "P2.log")]), baseline=parse_baseline("1=72"), metrics_only=True)
        self.assertIn("baseline --baseline-dps 1=72): 1 (A x3.41, B x0.34)", measured)
        self.assertIn("| Breakout builds per match (x3 the same-level bare build) | 1–2 | n=1 mean 1.00",
                      measured.split("## Metrics across")[1])
        # Fights at levels the baseline did not measure are left out, never extrapolated.
        self.assertIn("): 0; 2 fights outside the measured levels left out",
                      summarize(collect([str(self.root / "P2.log")]), baseline=parse_baseline("6=100,10=150"), metrics_only=True))
        self.assertIn("Outrageous hits (>= 60% of the target's max HP): 1", metrics)
        self.assertIn("Longest chain: 6 steps (1 of 1 chains >= 5)", metrics)
        self.assertIn("Damage factors: 7 hits, max factor_bp 120000; sources card_k06 2, card_big_one 1", metrics)
        self.assertIn("Reaction kinds: 1 (Overload)", metrics)
        self.assertIn("Cards offered: 3", metrics)
        self.assertIn("| Matches with an outrageous moment (hit ≥ 60% max HP or chain ≥ 5) | every match | 1 of 1 |", across)
        self.assertIn("Cards pick rate (picked / offered), 3 ids, 2 below 4%:", across)
        self.assertIn("| card_k06 | 1 | 1 | 100.0% |", across)
        self.assertIn("Actives pick rate: no data (none offered)", across)
        # With an older Host's match beside it, casts come only from the match that has LD_CAST lines.
        mixed = summarize(collect([str(self.root / "P2.log"), str(self.root / "Host.log")]), metrics_only=True)
        across = mixed.split("## Metrics across 2 matches with Host lines")[1]
        self.assertIn("| Casts per person per fight, any level | — | n=2 mean 2.0", across)
        self.assertIn("Casts leave out 1 matches without LD_CAST lines.", across)
        # A contender whose fights hold no release is flagged; B cast once at 11000.
        self.assertIn("| AI that never cast in any of their fights | 0 | 0 |", text)
        silent = self.root / "Silent.log"
        silent.write_text((self.root / "P2.log").read_text(encoding="utf-8").replace("LD_CAST t=11000", "LD_NOTE t=11000"),
                          encoding="utf-8")
        flagged = summarize(collect([str(silent)]), metrics_only=True)
        self.assertIn("  - AI that never cast in any of their fights: 1 (B 1 fights)", flagged)
        self.assertIn("| AI that never cast in any of their fights | 0 | 1 |", flagged)
        # Auto-empower: A pays twice 100 ms apart (a loop) and lands one tagged hit; B's basic empower on a camp misses.
        empower = lambda at, t, who, life, host, target, before, cost, serial: line(
            at, "LogLDCombat", "Log", f"LD_EMPOWER t={t} entity={who} life={life} host={host} skill=- target={target} "
                                      f"before={before} cost={cost} after={before - cost} serial={serial} payload=p")
        armed = {f"entity={a} life=1 ": " empower=2 armed_ms=5000", f"entity={b} life=1 ": " empower=1 armed_ms=1000"}
        ledgered = [row + next((extra for key, extra in armed.items() if "LD_MOMENTUM" in row and key in row), "")
                    for row in rows]
        empowered = ledgered[:-1] + [
            empower(10, 10000, a, 1, "Q", "contender", 70, 30, 1),
            empower(10, 10100, a, 1, "Q", "contender", 40, 30, 2),
            hit(10, 10200, a, b, "learned:active_zap", "10.00", "factor_bp=10000 factors=- empower=1"),
            empower(11, 11000, b, 1, "Basic", "camp", 60, 20, 1),
        ] + ledgered[-1:]
        (self.root / "Empower.log").write_text("\n".join(empowered) + "\n", encoding="utf-8")
        report = summarize(collect([str(self.root / "Empower.log")]), metrics_only=True)
        expected = ("Auto-empower: 3 triggers (9.00 per contender field minute); hosts Basic 33%, Q 67%; targets contender 67%, "
                    "camp 33%; paid and missed 67%; mean cost 26.7, mean momentum before 56.7; re-triggers within 250 ms 1; "
                    "most hits from one trigger 1; armed 30% of contender field time")
        self.assertIn(f"- {expected}", report.split("## Metrics across")[0])
        self.assertIn(f"- {expected}", report.split("## Metrics across")[1])
        # The mechanic is on (ledgers carry empower=) but nobody triggered: 0, not n/a.
        quiet_empower = [row for row in empowered if "LD_EMPOWER" not in row]
        (self.root / "EmpowerIdle.log").write_text("\n".join(quiet_empower) + "\n", encoding="utf-8")
        self.assertIn("- Auto-empower: 0 triggers; armed 30% of contender field time",
                      summarize(collect([str(self.root / "EmpowerIdle.log")]), metrics_only=True))
        # Field objectives: one serial ends, the other repeats its Open line every frame and never ends.
        objective = lambda serial, kind, phase: line(15, "LogLDWorld", "Display",
                                                     f"LD_OBJECTIVE serial={serial} kind={kind} phase={phase} anchor=a field_t_ms=1")
        stuck = [objective(1, "mini_flag", 1), objective(1, "mini_flag", 2), objective(1, "mini_flag", 3),
                 objective(2, "arena", 1), *[objective(2, "arena", 2)] * 4]
        (self.root / "Objectives.log").write_text("\n".join(rows[:-1] + stuck + rows[-1:]) + "\n", encoding="utf-8")
        lifecycle = summarize(collect([str(self.root / "Objectives.log")]), metrics_only=True)
        self.assertIn("- Field objectives: #1 mini_flag Announced→Open→Won; #2 arena Announced→Open, never ended, "
                      "3 repeated lines", lifecycle)
        self.assertIn("| Field objectives that ended (Won or Expired) | every one | 1 of 2; 3 repeated phase lines |", lifecycle)
        self.assertIn("- Field objectives: no data (no LD_OBJECTIVE line)", text)
        # A measured baseline is named in the report.
        measured = summarize(collect([str(self.root / "P2.log")]), baseline=parse_baseline("1=300"), metrics_only=True)
        self.assertIn("baseline --baseline-dps 1=300): 0 (A x0.82, B x0.08)", measured)
        # A finished match nobody fought in has no LD_HIT but still has its field share, alone and beside a fought match.
        quiet = [
            line(1, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=9 contenders=2 started_at_ms=0"),
            line(2, "LogLDMatch", "Display", "LD_FIELD_ENTER generation=9 segment=0 at_ms=2000 field_t_ms=0 final=0"),
            line(8, "LogLDMatch", "Display", "LD_FIELD_ENTER generation=9 segment=1 at_ms=8000 field_t_ms=6000 final=1"),
            line(10, "LogLDMatch", "Display", "LD_MATCH_RESULT generation=9 reason=1 winner=X ended_ms=10000"),
        ]
        (self.root / "Quiet.log").write_text("\n".join(quiet) + "\n", encoding="utf-8")
        alone = summarize(collect([str(self.root / "Quiet.log")]), metrics_only=True)
        self.assertIn("- Field time share (field segments before the showdown / whole match): 60.0% (6 s of 10 s)", alone)
        self.assertIn("Field time share (before the showdown / whole match): 60.0% (6 s of 10 s)", alone.split("## Metrics across")[1])
        both = summarize(collect([str(self.root / "P2.log"), str(self.root / "Quiet.log")]), metrics_only=True)
        self.assertIn("| ≥ 65% | 53.3% (16 s of 30 s) |", both.split("## Metrics across 1 matches with Host lines")[1])

    def test_winner_builds_lives_without_ledgers_and_reused_generations(self):
        def run(name, points, cards, with_ledger, folder=None):
            rows = [
                line(1, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=1 contenders=2 started_at_ms=0"),
                line(1, "LogLDMatch", "Display", f"LD_MATCH_HEADER generation=1 ledger={name} product=0.11.0"),
                line(1, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=1 slot=0 contender={AI} name=A kind=AI host=0 character=c address=ai"),
                line(1, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=1 slot=1 contender={CLIENT} name=B kind=AI host=0 character=c address=ai"),
                line(2, "LogLDTrace", "Log", f"LD_HEARTBEAT t=2000 contender={AI} phase=Alive life=1 pos=0,0,0"),
                line(2, "LogLDTrace", "Log", f"LD_HEARTBEAT t=2000 contender={CLIENT} phase=Alive life=1 pos=0,0,0"),
                line(3, "LogLDCombat", "Log",
                     f"LD_HIT t=3000 src={AI} src_kind=contender src_monster=- credit={AI} victim={CLIENT} victim_life=1 "
                     "victim_kind=contender victim_monster=- via=basic class=basic type=physical dmg=10.00 absorbed=0.00 "
                     "hp_lost=10.00 hp=860.00->850.00 max_hp=860.00 shield=0.00->0.00 amp_bp=10000 resist=0.00 crit=0 "
                     "protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal=0 pool_bp=0"),
                line(9, "LogLDMatch", "Display", f"LD_MATCH_SUMMARY generation=1 reason=GuardSucceeded winner={AI} duration_ms=9000"),
                line(9, "LogLDMatch", "Display", f"LD_MATCH_SUMMARY_ROW generation=1 contender={AI} name=A kind=AI left=0 winner=1 "
                     f"level=3 xp=1 kills=0 deaths=0 assists=0 choices=1 skills=active_zap/Q blueprints=- points={points} "
                     f"cards={cards}"),
            ]
            if with_ledger:
                rows.append(line(9, "LogLDCombat", "Log", f"LD_MOMENTUM entity={CLIENT} life=1 basic=10.00 combat=0.00 skill=0.00 "
                                 "reaction=0.00 other=0.00 idle=0.00 spent=0.00 end=60.00"))
            target = self.root / (folder or ".") / f"{name}.log"
            target.parent.mkdir(exist_ok=True)
            target.write_text("\n".join(rows) + "\n", encoding="utf-8")
            return str(target)

        first = run("RunA", "3,0,0,0,0", "+medal_fire#1@Skill:active_zap,card_spiky_hide#2", True)
        second = run("RunB", "0,0,3,0,0", "+medal_fire#1@Skill:active_zap", False)
        text = summarize(collect([first, second]), metrics_only=True)
        # Both Host processes started at generation 1: two matches, not one merged match.
        self.assertIn("## Match 1 (RunA)", text)
        self.assertIn("## Match 1 (RunB)", text)
        across = text.split("## Metrics across 2 matches with Host lines")[1]
        # Same actives, different points: two builds; a card in the library does not count.
        self.assertIn("| 2 in 2 decided matches |", across)
        # RunA closed B's ledger and left A alive; RunB closed none.
        self.assertIn("incomplete: 3 field lives have no ledger (alive at the match end)", across)
        same = summarize(collect([first, run("RunC", "3,0,0,0,0", "+medal_fire#7@Skill:active_zap", True)]), metrics_only=True)
        self.assertIn("| 1 in 2 decided matches |", same)
        # The same card on another place is another build, and so is a second copy.
        moved = summarize(collect([first, run("RunD", "3,0,0,0,0", "+medal_fire#1@Skill:active_snark_bolt", True)]),
                          metrics_only=True)
        self.assertIn("| 2 in 2 decided matches |", moved)
        doubled = summarize(collect([first, run("RunE", "3,0,0,0,0", "+medal_fire#1@Skill:active_zap,+medal_fire#3@Skill:active_zap",
                                                True)]), metrics_only=True)
        self.assertIn("| 2 in 2 decided matches |", doubled)
        # Y25 with another element is another build.
        rewired = summarize(collect([run("RunF", "3,0,0,0,0", "+card_live_wire#1@Body:Fire", True),
                                     run("RunG", "3,0,0,0,0", "+card_live_wire#1@Body:Water", True)]), metrics_only=True)
        self.assertIn("| 2 in 2 decided matches |", rewired)
        # Two runs' logs of the same file name in different folders are two inputs, never one match.
        apart = summarize(collect([run("Telemetry", "3,0,0,0,0", "+medal_fire#1@Skill:active_zap", True, folder="run1"),
                                   run("Telemetry", "3,0,0,0,0", "+medal_fire#1@Skill:active_zap", True, folder="run2")]),
                          metrics_only=True)
        self.assertIn("## Match 1 (Telemetry)", apart)
        self.assertIn("## Match 1 (Telemetry (run2))", apart)
        self.assertIn("## Metrics across 2 matches with Host lines", apart)

    def test_baseline_points_interpolate_only_within_measured_levels(self):
        points = parse_baseline("10=154,1=72")
        self.assertEqual(points, ((1, 72.0), (10, 154.0)))
        self.assertAlmostEqual(baseline_at(points, 1), 72.0)
        self.assertAlmostEqual(baseline_at(points, 10), 154.0)
        self.assertAlmostEqual(baseline_at(points, 4), 72.0 + 82.0 / 3)
        self.assertIsNone(baseline_at(points, 16))
        three = parse_baseline("1=70,6=100,10=180")
        self.assertAlmostEqual(baseline_at(three, 8), 140.0)
        self.assertAlmostEqual(baseline_at(three, 3), 82.0)
        self.assertEqual(baseline_at(parse_baseline("1=50"), 1), 50.0)
        self.assertIsNone(baseline_at(parse_baseline("1=50"), 9))
        for bad in ("1=0", "1=70,1=80", "x"):
            with self.assertRaises(ValueError):
                parse_baseline(bad)

    def test_baseline_run_measures_only_valid_bare_mirror_cases(self):
        keep = {"keep_points_unspent": True}
        run = self.root / "run"
        run.mkdir()
        (run / "manifest.json").write_text(json.dumps({"design": {"arms": [
            {"id": "bare", "plan": {"subject": keep, "opponent": keep}},
            {"id": "sample", "plan": {"subject": {"breakout_preset": "x"}, "opponent": keep}}]}}), encoding="utf-8")

        def case(arm, status="Completed", ttk=20000, censored=False, level=16, dealt=2735.0, taken=1265.0):
            return json.dumps({"arm": arm, "status": status, "facts": {"builds": {"subject": {"level": level},
                                                                                  "opponent": {"level": level}}},
                               "metrics": {"engage_to_kill_ms": {"value": ttk, "censored": censored},
                                           "damage_dealt_hp": {"value": dealt}, "damage_taken_hp": {"value": taken}}})

        (run / "cases.jsonl").write_text("\n".join([
            case("bare"),  # (2735 + 1265) / 2 over 20 s = 100 DPS
            case("bare", ttk=90000, censored=True), case("bare", status="Invalid"), case("sample", ttk=4000),
            case("bare", level=6, ttk=10000, dealt=1500.0, taken=500.0)]) + "\n", encoding="utf-8")
        points, source = baseline_from_runs([str(run)])
        self.assertEqual(points, ((6, 100.0), (16, 100.0)))
        self.assertIn("L16 100.0 DPS (n=1)", source)
        empty = self.root / "empty"
        empty.mkdir()
        (empty / "manifest.json").write_text((run / "manifest.json").read_text(encoding="utf-8"), encoding="utf-8")
        (empty / "cases.jsonl").write_text(case("bare", status="Invalid") + "\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            baseline_from_runs([str(empty)])

if __name__ == "__main__":
    unittest.main()
