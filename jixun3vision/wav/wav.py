"""Non-blocking prerecorded ring-score voice reporting."""

import os


class RingVoiceReporter:
    """Play one prerecorded score per shot without blocking the frame loop.

    ``report`` is idempotent for a shot number. Invalid/unknown scores are
    ignored and do not consume the shot, so a later valid final result can
    still be reported. The Maix audio module is imported lazily to keep this
    class unit-testable on a desktop.
    """

    def __init__(self, voice_dir=None, volume=80, player_factory=None):
        self.voice_dir = voice_dir or os.path.dirname(os.path.abspath(__file__))
        self.volume = max(0, min(100, int(volume)))
        self.player_factory = player_factory
        self.player = None
        self.reported_shots = {}
        self.play_count = 0
        self.last_score = -1
        self.last_shot = 0
        self.last_error = ""

    @staticmethod
    def _default_player_factory(path):
        from maix import audio
        return audio.Player(path, block=False)

    def score_path(self, score):
        return os.path.join(self.voice_dir, "ring_%d.wav" % int(score))

    @staticmethod
    def _release_player(player):
        if player is None:
            return
        try:
            stop = getattr(player, "stop", None)
            if stop is not None:
                stop()
        except Exception:
            pass
        for method_name in ("close", "deinit"):
            method = getattr(player, method_name, None)
            if method is not None:
                try:
                    method()
                except Exception:
                    pass
                break

    def close(self):
        player = self.player
        self.player = None
        self._release_player(player)

    def report(self, shot_number, score):
        """Start one non-blocking playback; return True only when started."""
        try:
            shot_number = int(shot_number)
            score = int(score)
        except (TypeError, ValueError):
            return False
        if shot_number < 1 or score < 1 or score > 10:
            return False
        if shot_number in self.reported_shots:
            return False

        path = self.score_path(score)
        if not os.path.isfile(path):
            self.last_error = "voice file missing: %s" % path
            print(self.last_error)
            return False

        factory = self.player_factory or self._default_player_factory
        player = None
        try:
            self.close()
            player = factory(path)
            try:
                player.volume(self.volume)
            except Exception:
                pass
            player.play()
            # Keep the player alive for the asynchronous playback lifetime.
            self.player = player
            self.reported_shots[shot_number] = score
            self.play_count += 1
            self.last_score = score
            self.last_shot = shot_number
            self.last_error = ""
            return True
        except Exception as error:
            self._release_player(player)
            self.last_error = str(error)
            print("voice play failed: %s %s" % (path, error))
            return False

    def reset_session(self):
        """Allow the same shot numbers to be spoken in a restarted task."""
        self.close()
        self.reported_shots = {}
        self.last_score = -1
        self.last_shot = 0
        self.last_error = ""

    # Compatibility with the old incomplete helper.
    def reset(self):
        self.reset_session()
