import unittest

import SimulateComponentAcquisition as Sim


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


if __name__ == "__main__":
    unittest.main()
