import os
import tempfile
import unittest

from wav import RingVoiceReporter


class FakePlayer:
    def __init__(self, path):
        self.path = path
        self.volume_value = None
        self.play_count = 0
        self.stop_count = 0

    def volume(self, value):
        self.volume_value = value

    def play(self):
        self.play_count += 1

    def stop(self):
        self.stop_count += 1


class PlayerFactory:
    def __init__(self):
        self.players = []

    def __call__(self, path):
        player = FakePlayer(path)
        self.players.append(player)
        return player


class RingVoiceReporterTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        for score in range(1, 11):
            path = os.path.join(self.temp_dir.name, "ring_%d.wav" % score)
            with open(path, "wb") as stream:
                stream.write(b"RIFF-test")
        self.factory = PlayerFactory()
        self.reporter = RingVoiceReporter(
            voice_dir=self.temp_dir.name,
            volume=65,
            player_factory=self.factory,
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_valid_final_score_starts_nonblocking_factory_once(self):
        self.assertTrue(self.reporter.report(1, 9))
        self.assertEqual(len(self.factory.players), 1)
        self.assertEqual(self.factory.players[0].play_count, 1)
        self.assertEqual(self.factory.players[0].volume_value, 65)
        self.assertTrue(self.factory.players[0].path.endswith("ring_9.wav"))

    def test_same_shot_is_reported_only_once_even_if_score_changes(self):
        self.assertTrue(self.reporter.report(1, 8))
        self.assertFalse(self.reporter.report(1, 9))
        self.assertEqual(len(self.factory.players), 1)
        self.assertEqual(self.reporter.reported_shots, {1: 8})

    def test_zero_unknown_and_out_of_range_scores_are_ignored(self):
        for score in (-1, 0, 11, None):
            self.assertFalse(self.reporter.report(1, score))
        self.assertEqual(len(self.factory.players), 0)
        self.assertEqual(self.reporter.reported_shots, {})

    def test_two_shots_can_report_same_ring_independently(self):
        self.assertTrue(self.reporter.report(1, 10))
        first_player = self.factory.players[0]
        self.assertTrue(self.reporter.report(2, 10))
        self.assertEqual(len(self.factory.players), 2)
        self.assertEqual(self.reporter.reported_shots, {1: 10, 2: 10})
        self.assertEqual(first_player.stop_count, 1)

    def test_task_restart_allows_shot_to_be_reported_again(self):
        self.assertTrue(self.reporter.report(1, 7))
        old_player = self.factory.players[0]
        self.reporter.reset_session()
        self.assertEqual(old_player.stop_count, 1)
        self.assertTrue(self.reporter.report(1, 7))
        self.assertEqual(len(self.factory.players), 2)

    def test_missing_file_does_not_consume_shot(self):
        os.remove(os.path.join(self.temp_dir.name, "ring_6.wav"))
        self.assertFalse(self.reporter.report(1, 6))
        self.assertNotIn(1, self.reporter.reported_shots)


if __name__ == "__main__":
    unittest.main()
