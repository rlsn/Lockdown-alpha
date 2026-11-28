"""Unit tests for Lockdown Gameplay Research & Experimentation Lab."""

import unittest

import GameplayResearchLab as lab


class TestGameplayResearchLab(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = lab.CatalogModel.load()

    def test_catalog_alignment(self):
        self.assertEqual(len(self.catalog.content["abilities"]), 24)
        self.assertEqual(len(self.catalog.build_cards_pool()), 85)
        self.assertEqual(len(self.catalog.medals_pool()), 5)
        self.assertEqual(len(self.catalog.content["camps"]["placements"]), 34)
        self.assertEqual(self.catalog.xp_for_level(16), 5340)
        self.assertEqual(self.catalog.card_capacity_at_level(1), 6)
        self.assertEqual(self.catalog.card_capacity_at_level(16), 11)

    def test_interaction_graph_extraction(self):
        graph = lab.extract_interaction_graph(self.catalog)
        self.assertGreaterEqual(graph["metadata"]["node_count"], 120)
        self.assertGreaterEqual(graph["metadata"]["edge_count"], 250)
        self.assertEqual(graph["catalog_structure"]["abilities_total"], 24)
        self.assertEqual(graph["catalog_structure"]["abilities_with_base_element"], 6)
        self.assertGreaterEqual(len(graph["closed_feedback_loops"]), 5)

    def test_stat_projection_and_deterministic_duel(self):
        spec = lab.make_archetype_combatant(self.catalog, "skill_engine", 12, 1.0)
        st = lab.project_stats(self.catalog, spec)
        self.assertGreater(st["max_hp"], 2200.0)
        self.assertEqual(st["ks_brains"], 1.0)
        bare = lab.make_archetype_combatant(self.catalog, "bare", 12, 1.0)
        r1 = lab.run_single_duel(self.catalog, spec, bare, seed=42)
        r2 = lab.run_single_duel(self.catalog, spec, bare, seed=42)
        self.assertEqual(r1, r2)
        self.assertEqual(r1["winner"], 1)

    def test_draft_and_ffa_reproducibility(self):
        d1 = lab.simulate_draft_and_formation(self.catalog, seed=101, trials_per_regime=120)
        d2 = lab.simulate_draft_and_formation(self.catalog, seed=101, trials_per_regime=120)
        self.assertEqual(d1["regimes"]["S0_CurrentBaseline"], d2["regimes"]["S0_CurrentBaseline"])
        self.assertGreater(
            d1["regimes"]["S4_HybridGuidedWithPivot"]["transformative_formation_rate"],
            d1["regimes"]["S0_CurrentBaseline"]["transformative_formation_rate"],
        )

        f1 = lab.simulate_ffa_tournament(self.catalog, seed=202, matches_per_setting=24)
        f2 = lab.simulate_ffa_tournament(self.catalog, seed=202, matches_per_setting=24)
        self.assertEqual(f1["player_count_sensitivity"]["P8"], f2["player_count_sensitivity"]["P8"])


if __name__ == "__main__":
    unittest.main()
