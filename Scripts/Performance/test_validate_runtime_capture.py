import copy
import unittest

from validate_runtime_capture import validate, validate_gpu_timing


class RuntimeCaptureTests(unittest.TestCase):
    def setUp(self):
        self.report = {
            "schemaVersion": 1,
            "status": "passed",
            "build": {"configuration": "DevDist"},
            "workload": {"measuredFrames": 3},
            "applicationTiming": {
                "scope": "application_render_submission_intervals_not_displayed_fps",
                "count": 3,
                "intervalsMilliseconds": [8.0, 9.0, 10.0],
                "elapsedMilliseconds": 27.0,
                "averageMilliseconds": 9.0,
                "medianMilliseconds": 9.0,
                "p95Milliseconds": 10.0,
                "p99Milliseconds": 10.0,
                "maximumMilliseconds": 10.0,
            },
        }

    def test_configuration_specific_gate(self):
        self.assertTrue(validate(self.report)["meanTargetMet"])
        self.report["build"]["configuration"] = "Dist"
        self.assertFalse(validate(self.report)["meanTargetMet"])

    def test_gpu_samples_keep_stalls_and_allow_sparse_coverage(self):
        timing = {
            "scope": "sampled_completed_gpu_frames_not_complete_timeline",
            "available": True,
            "count": 2,
            "samples": [{"frame": 101, "milliseconds": 0.0},
                        {"frame": 103, "milliseconds": 400.0}],
            "averageMilliseconds": 200.0,
            "medianMilliseconds": 0.0,
            "p95Milliseconds": 400.0,
            "p99Milliseconds": 400.0,
            "maximumMilliseconds": 400.0,
        }
        validate_gpu_timing(timing, 3)
        for field, value in (("count", True), ("available", False),
                             ("maximumMilliseconds", 10.0)):
            altered = copy.deepcopy(timing)
            altered[field] = value
            with self.assertRaises(ValueError):
                validate_gpu_timing(altered, 3)
        for frame, duration in ((101, 8.0), (True, 8.0), (104, -1.0),
                                (104, float("nan")), (104, True)):
            altered = copy.deepcopy(timing)
            altered["samples"][1] = {"frame": frame, "milliseconds": duration}
            with self.assertRaises(ValueError):
                validate_gpu_timing(altered, 3)

    def test_unavailable_gpu_timing_is_not_zero_cost(self):
        timing = {"scope": "sampled_completed_gpu_frames_not_complete_timeline",
                  "available": False, "count": 0, "samples": []}
        validate_gpu_timing(timing, 3)
        timing["averageMilliseconds"] = 0.0
        with self.assertRaises(ValueError):
            validate_gpu_timing(timing, 3)

    def test_unfinished_capture(self):
        self.report["status"] = "running"
        with self.assertRaises(ValueError):
            validate(self.report)

    def test_resolution_change_diagnostic_is_not_a_valid_capture(self):
        report = {
            "schemaVersion": 1,
            "status": "failed",
            "reason": "drawable_resolution_changed",
            "expectedWidth": 1280,
            "expectedHeight": 720,
            "observedWidth": 3440,
            "observedHeight": 1392,
            "measuredIntervalCount": 1500,
        }
        with self.assertRaises(ValueError):
            validate(report)

    def test_missing_samples_and_forged_summary(self):
        for field, value in (("count", 2), ("averageMilliseconds", 1.0),
                             ("maximumMilliseconds", 9.0), ("scope", "gpu_latency")):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report["applicationTiming"][field] = value
                with self.assertRaises(ValueError):
                    validate(report)

    def test_invalid_intervals(self):
        for value in (0.0, -1.0, float("nan"), float("inf"), True, "8"):
            with self.subTest(value=value):
                report = copy.deepcopy(self.report)
                report["applicationTiming"]["intervalsMilliseconds"][0] = value
                with self.assertRaises(ValueError):
                    validate(report)


if __name__ == "__main__":
    unittest.main()
