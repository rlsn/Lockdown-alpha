import unittest

import SimulateComponentAcquisition as Sim


def content_row(content, component):
    return next(item for item in content["components"] if item["id"] == component)


class SimulateComponentAcquisitionTests(unittest.TestCase):
    def test_every_catalog_entry_has_a_semantic_tag(self):
        content = Sim.load(Sim.CONTENT)
        Sim.Catalog(content, filtered=True)

    def test_hostable_filter_only_draws_installable_definitions(self):
        content = Sim.load(Sim.CONTENT)
        cat = Sim.Catalog(content, filtered=True)
        rng = Sim.random.Random(7)
        actives = ["active_snark_bolt"]
        owned: set[str] = set()
        for level in range(2, cat.max_level + 1):
            drawn = cat.draw(rng, level, actives, owned)
            if drawn is None:
                break
            self.assertTrue(cat.hosts(drawn, actives, owned), drawn)
            owned.add(drawn)

    def test_mod_targets_a_same_mode_active_only_when_it_changes_it(self):
        content = Sim.load(Sim.CONTENT)
        cat = Sim.Catalog(content, filtered=True)
        self.assertNotIn("active_zap", content_row(content, "component_crowd_hit")["targets"]["actives"])
        self.assertIn("active_zap", cat.hosts("component_crowd_hit", ["active_zap"], set()))
        self.assertNotIn("active_pants_down", cat.hosts("component_skewer", ["active_pants_down"], set()))
        self.assertNotIn("active_open_mic", cat.hosts("component_crowd_hit", ["active_open_mic"], set()))

    def test_draw_requirement_needs_one_listed_active_learned(self):
        content = Sim.load(Sim.CONTENT)
        cat = Sim.Catalog(content, filtered=True)
        self.assertTrue(cat.hosts("component_move_meeting", ["active_seethe"], set()))
        self.assertFalse(cat.drawable("component_move_meeting", ["active_seethe"], set()))
        self.assertFalse(cat.drawable("component_set_it_off", ["active_seethe", "active_flyer_trap"], set()))
        self.assertTrue(cat.drawable("component_move_meeting", ["active_seethe", "active_flyer_trap"], set()))
        self.assertTrue(cat.drawable("component_set_it_off", ["active_seethe", "active_dirty_puddle"], set()))
        rng = Sim.random.Random(11)
        drawn = {cat.draw(rng, 14, ["active_seethe"], set()) for _ in range(2000)}
        self.assertFalse({"component_move_meeting", "component_set_it_off"} & drawn)

    def test_runs_are_seed_deterministic(self):
        content = Sim.load(Sim.CONTENT)
        a = Sim.simulate(content, runs=40, seed=3, policy="random", filtered=True, elite_levels=[])
        b = Sim.simulate(content, runs=40, seed=3, policy="random", filtered=True, elite_levels=[])
        self.assertEqual(a, b)

    def test_signature_ties_split_equally_without_name_bias(self):
        cat = Sim.Catalog(Sim.load(Sim.CONTENT), filtered=True)
        cat.components = {
            "one": {"tags": ["a", "b", "c"]},
            "two": {"tags": ["a"]},
            "three": {"tags": ["d"]},
        }
        self.assertEqual(Sim.tag_signature_candidates(cat, set()), [()])
        self.assertEqual(Sim.tag_signature_candidates(cat, {"one"}), [("a", "b"), ("a", "c"), ("b", "c")])
        self.assertEqual(Sim.tag_signature_candidates(cat, {"one", "two"}), [("a", "b"), ("a", "c")])
        self.assertEqual(Sim.tag_signature_candidates(cat, {"one", "two", "three"}),
                         [("a", "b"), ("a", "c"), ("a", "d")])
        self.assertEqual(Sim.tag_signature(cat, {"one", "two", "three"}), ("a", "b"))

    def test_signature_summary_splits_each_run_not_each_candidate(self):
        cat = Sim.Catalog(Sim.load(Sim.CONTENT), filtered=True)
        run = Sim.run_one(cat, Sim.random.Random(3), "random", [])
        first = dict(run, signature=("a", "b"), signature_candidates=[("a", "b"), ("a", "c")])
        second = dict(run, signature=("a", "b"), signature_candidates=[("a", "b")])
        summary = Sim.summarize([first, second])
        self.assertEqual(summary["signatures"]["top"], [["a+b", 1.0]])
        neutral = summary["signatures_tie_neutral"]
        self.assertEqual(neutral["top"], [["a+b", 0.75], ["a+c", 0.25]])
        self.assertEqual(neutral["ambiguous_run_share"], 0.5)

    def test_installs_stay_within_the_workshop_capacity_and_bonus_levels_draw_twice(self):
        content = Sim.load(Sim.CONTENT)
        cat = Sim.Catalog(content, filtered=True)
        rng = Sim.random.Random(5)
        for _ in range(30):
            run = Sim.run_one(cat, rng, "synergy", [])
            self.assertLessEqual(run["installed"], cat.capacity(cat.max_level, False))
            self.assertLessEqual(run["owned"], 1 + sum(cat.draws(level, False) for level in range(2, cat.max_level + 1)))
        legacy = Sim.run_one(cat, rng, "synergy", [], legacy=True)
        self.assertLessEqual(legacy["owned"], cat.max_level)
        self.assertEqual(legacy["benched"], 0)

    def test_installed_selector_respects_body_column_capacity(self):
        content = Sim.load(Sim.CONTENT)
        cat = Sim.Catalog(content, filtered=True)
        body = []
        forms = set()
        for cid in cat.common:
            component = cat.components[cid]
            form = cat.form(cid)
            if component["kind"] == "base" and not component.get("binding") and (not form or form not in forms):
                body.append(cid)
                if form:
                    forms.add(form)
        self.assertGreater(len(body), cat.body_column_capacity)
        placed = {cid: "self" for cid in body[:cat.body_column_capacity]}
        self.assertIsNone(Sim.place(cat, body[cat.body_column_capacity], [], set(body), placed,
                                    body_capacity=cat.body_column_capacity,
                                    dash_capacity=cat.dash_column_capacity))

    def test_dismantle_pool_only_draws_components_that_link_to_installed_build(self):
        content = Sim.load(Sim.CONTENT)
        cat = Sim.Catalog(content, filtered=True)
        for host in cat.common:
            compatible = [
                cid for cid in cat.common
                if cid != host and host in (
                    set(cat.components[cid].get("targets", {}).get("components", []))
                    | set(cat.components[cid].get("targets", {}).get("bases", []))
                )
            ]
            if not compatible:
                continue
            unrelated = next(
                cid for cid in cat.common
                if cid != host and host not in (
                    set(cat.components[cid].get("targets", {}).get("components", []))
                    | set(cat.components[cid].get("targets", {}).get("bases", []))
                )
            )
            pool = cat.dismantle_pool({host}, {host})
            self.assertIn(compatible[0], pool)
            self.assertNotIn(unrelated, pool)
            self.assertEqual(cat.dismantle_pool({host}, set()), [])
            return
        self.fail("catalog has no component-to-component dismantle links")


if __name__ == "__main__":
    unittest.main()
