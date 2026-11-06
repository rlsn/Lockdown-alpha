import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from SummarizePlaytestLog import check_client, collect, main, parse_line, summarize

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
    line(9.9, "LogLDGrowth", "Log", f"LD_OFFER t=0 contender={AI} ordinal=1 rerolled=0 level=1 cards=LearnActive:active_lunge:0>1"),
    line(10, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=7 contenders=3 open_at_ms=480000"),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_HEADER generation=7 ledger=F product=0.10.0 wire=6 commit={'a' * 40} dirty=0 "
         f"catalog=abcdef0123456789 net=ListenServer address=\"192.168.1.2:7777\" contenders=3 ai=1 open_at_ms=480000 dev_test=0 dev_reload=0"),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=7 slot=0 contender={HOST} name=\u623f\u4e3b kind=Human host=1 character=c address=local"),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=7 slot=1 contender={CLIENT} name=\"Little \\\"Wang\\\"\" kind=Human host=0 character=c address=\"127.0.0.1:5555\""),
    line(10, "LogLDMatch", "Display", f"LD_MATCH_ROSTER generation=7 slot=2 contender={AI} name=\"AI 1\" kind=AI host=0 character=c address=ai"),
    line(11, "LogLDTrace", "Log", f"LD_HEARTBEAT t=1000 contender={CLIENT} phase=Alive life=1 pos=1,2,3 hp=500.00/500.00 mp=1.00/1.00 shield=0.00 level=1 xp=0 gold=200 pending=1 order=none order_at=- action=- statuses=- protected_until=0 respawn_at=0"),
    line(12, "LogLDGrowth", "Log", f"LD_OFFER t=2000 contender={CLIENT} ordinal=1 rerolled=0 level=1 cards=LearnActive:active_arc_bolt:0>1,Attribute:attr_might:0>1,LearnPassive:passive_x:0>1"),
    line(13, "LogLDGrowth", "Log", f"LD_CHOICE t=3000 contender={CLIENT} ordinal=1 kind=LearnActive card=active_arc_bolt rank=1 replaced=- level=1 pending=0"),
    line(20, "LogLDCombat", "Log", f"LD_HIT t=10000 src={AI} src_kind=contender src_monster=- credit={AI} victim={CLIENT} victim_life=1 victim_kind=contender victim_monster=- via=basic class=basic type=physical dmg=60.00 pre=60.00 absorbed=10.00 shattered=0.00 hp_lost=50.00 hp=500.00->450.00 max_hp=500.00 shield=10.00->0.00 amp_bp=10000 resist=10.00 mitigate_bp=0 crit=0 backstab=0 protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal=0"),
    line(25, "LogLDCombat", "Log", f"LD_HIT t=15000 src={AI} src_kind=contender src_monster=- credit={AI} victim={CLIENT} victim_life=1 victim_kind=contender victim_monster=- via=learned:active_lunge class=ability type=physical dmg=450.00 pre=450.00 absorbed=0.00 shattered=0.00 hp_lost=450.00 hp=450.00->0.00 max_hp=500.00 shield=0.00->0.00 amp_bp=10000 resist=10.00 mitigate_bp=0 crit=1 backstab=0 protected=0 periodic=0 reflect=0 added=0 controls=stun vamp=0.00 lethal=1"),
    line(26, "LogLDCombat", "Log", f"LD_HIT t=16000 src={HOST} src_kind=contender src_monster=- credit={HOST} victim={CAMP} victim_life=1 victim_kind=camp victim_monster=monster_scavenger via=basic class=basic type=physical dmg=5.00 pre=5.00 absorbed=0.00 shattered=0.00 hp_lost=5.00 hp=80.00->75.00 max_hp=80.00 shield=0.00->0.00 amp_bp=10000 resist=0.00 mitigate_bp=0 crit=0 backstab=0 protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal=0"),
    line(25.5, "LogLDMatch", "Display", f"LD_DEATH_RECEIPT contender={CLIENT} life=1 respawn_at_ms=21000 t=15000 killer={AI} cause=Contender fatal=learned:active_lunge fatal_body={AI} assisters={HOST} no_reward=None victim_level=1 bounty=0"),
    line(25.5, "LogLDGrowth", "Log", f"LD_REWARD t=15000 contender={AI} cause=death role=killer gold=100 xp=80 gold_after=300 xp_after=80 level=1->2 victim={CLIENT} victim_life=1 victim_kind=contender"),
    line(25.5, "LogLDGrowth", "Log", f"LD_LEVEL_UP t=15000 contender={AI} level=1->2 choices_earned=1"),
    line(30, "LogLDGrowth", "Log", f"LD_SHOP_TRADE t=20000 contender={HOST} kind=Buy item=item_edge_blade slot=1 gold_delta=-150 gold_after=50 request=1"),
    line(31, "LogLDMatch", "Log", f"LD_CAMP_DEATH member={CAMP} life=1 monster=monster_scavenger t=21000 killer={HOST} fatal=basic no_reward=None"),
    line(31, "LogLDGrowth", "Log", f"LD_REWARD t=21000 contender={HOST} cause=death role=killer gold=30 xp=40 gold_after=80 xp_after=40 level=1->1 victim={CAMP} victim_life=1 victim_kind=camp"),
    line(31.5, "LogLDNav", "Display", "LD_NAV_ACCEPT match=7 life=1 order=3 epoch=0 kind=0 goal=X=0"),
    line(32, "LogLDMatch", "Display", f"LD_PLAYER_MARK source=host_view contender={CLIENT} name=\"Little \\\"Wang\\\"\" seq=1 t=22000 client_t=21950 phase=Dead pos=-"),
    line(40, "LogLDNet", "Log", "LD_PERF role=host window_s=10.0 fps=60.0 frame_ms=16.67 peak_ms=40.0 frames=600 hitches=0 hitch_ms=100"),
    line(40, "LogLDNet", "Log", f"LD_NET role=host contender={CLIENT} address=\"127.0.0.1:5555\" rtt_ms=40 jitter_ms=3.5 loss_in=1.0 loss_out=0.0 since_recv_s=0.01 corrections_sent=2"),
    line(41, "LogLDNet", "Log", "LD_PERF_HITCH role=host frame_ms=180.5 t=31000 suppressed=2"),
    line(42, "LogLDCombat", "Error", "LD_DEMO_AUTHORITY_FAULT something broke"),
    line(50, "LogLDMatch", "Display", "LD_MATCH_RESULT generation=7 reason=1 winner=X ended_ms=40000"),
    line(50, "LogLDMatch", "Display", f"LD_MATCH_SUMMARY generation=7 reason=GuardSucceeded winner={AI} started_ms=0 ended_ms=40000 duration_ms=40000"),
    line(50, "LogLDMatch", "Display", f"LD_MATCH_SUMMARY_ROW generation=7 contender={AI} name=\"AI 1\" kind=AI left=0 winner=1 level=2 xp=80 gold=300 kills=1 deaths=0 assists=0 choices=1 skills=active_lunge@1/Q cards=- equipment=-"),
    line(55, "LogLDMatch", "Display", "LD_ROOM_RESET generation=8 aborted_start=0"),
    line(60, "LogLDMatch", "Display", "LD_MATCH_STARTED generation=8 contenders=2 open_at_ms=480000"),
]

CLIENT_LOG = [
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_MATCH match=7 contender={CLIENT} name=\"Little \\\"Wang\\\"\" product=0.10.0 wire=6 commit={'a' * 40} server=\"192.168.1.2:7777\""),
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_ROSTER match=7 slot=0 contender={HOST} name=\u623f\u4e3b kind=Human host=1 self=0"),
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_ROSTER match=7 slot=1 contender={CLIENT} name=\"Little \\\"Wang\\\"\" kind=Human host=0 self=1"),
    line(10.2, "LogLDMatch", "Display", f"LD_SELF_ROSTER match=7 slot=2 contender={AI} name=\"AI 1\" kind=AI host=0 self=0"),
    line(11.2, "LogLDTrace", "Log", "LD_SELF_STATE t=1000 match=7 life=1 phase=Alive pos=1,2,3 hp=500.00/500.00 mp=1.00/1.00 shield=0.00 level=1 xp=0 gold=200 pending=1 order=none action=- statuses=-"),
    line(12.2, "LogLDGrowth", "Log", "LD_SELF_OFFER t=2000 ordinal=1 rerolled=0 level=1 cards=LearnActive:active_arc_bolt:0>1"),
    line(20.2, "LogLDCombat", "Log", "LD_SELF_HP t=10000 kind=shield_absorbed amount=10 hp=450.00/500.00 shield=0.00"),
    line(20.2, "LogLDCombat", "Log", "LD_SELF_HP t=10000 kind=damage amount=50 hp=450.00/500.00 shield=0.00"),
    line(24.0, "LogLDCombat", "Log", f"LD_SELF_SEEN_CAST t=14000 cast=\"Lunge\" unit={AI} pos=5,5,0"),
    line(25.2, "LogLDCombat", "Log", "LD_SELF_STATUS t=15000 added=stun:500 removed=- controlled=1"),
    line(25.7, "LogLDMatch", "Display", f"LD_SELF_KILLFEED serial=1 t=15000 victim={CLIENT} victim_name=\"Little \\\"Wang\\\"\" killer={AI} killer_name=\"AI 1\" cause=Contender assisters={HOST}"),
    line(25.7, "LogLDMatch", "Display", f"LD_SELF_DEATH t=15000 killer={AI} killer_name=\"AI 1\" cause=Contender assisters={HOST}"),
    line(32.0, "LogLDMatch", "Display", f"LD_PLAYER_MARK source=self seq=1 contender={CLIENT} t=21950 match=7 life=1 phase=Dead pos=- hp=- mp=- shield=- level=1 xp=0 gold=200 pending=0 order=none action=- statuses=-"),
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

    def test_host_log_alone_gives_roster_deaths_economy_marks_and_network(self):
        text = summarize(collect([str(self.root / "Host.log")]))
        self.assertIn("## Match 7", text)
        self.assertIn("## Match 8", text)
        self.assertIn("- 1: Little \"Wang\" [Human]", text)
        self.assertIn("- 0: \u623f\u4e3b [Human (host)]", text)
        self.assertIn("Little \"Wang\" died to AI 1 via learned:active_lunge (assists \u623f\u4e3b; no_reward None)", text)
        # Both hits on the victim inside the death window, the unrelated camp hit is not listed under the death.
        deaths = text.split("### Deaths")[1].split("###")[0]
        self.assertIn("AI 1 -> Little \"Wang\" basic basic dmg 60.00 (shield 10.00) hp 500.00->450.00", deaths)
        self.assertIn("AI 1 -> Little \"Wang\" learned:active_lunge ability dmg 450.00", deaths)
        self.assertIn("[crit, stun]", deaths)
        self.assertNotIn("camp:monster_scavenger", deaths)
        economy = text.split("### Economy")[1].split("###")[0]
        for expected in ("offer #1", "chose active_arc_bolt rank 1", "+100 gold +80 xp (death killer of Little \"Wang\")",
                         "AI 1: level 1->2 (+1 choices)", "Buy item_edge_blade slot 1 gold -150 -> 50",
                         "00:00.0 AI 1: offer #1: LearnActive:active_lunge:0>1",
                         "+30 gold +40 xp (death killer of camp:monster_scavenger)"):
            self.assertIn(expected, economy)
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
        self.assertIn("offer #1", text.split("### Economy")[1])
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
            line(12, "LogLDMatch", "Display", f"LD_DEATH_RECEIPT contender={CLIENT} life=1 respawn_at_ms=1 t=1 killer={HOST} cause=Contender fatal=basic fatal_body={HOST} assisters=- no_reward=None victim_level=1 bounty=0"),
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


if __name__ == "__main__":
    unittest.main()
