"""MaixCAM2 competition vision entry point.

Hardware initialization and per-frame orchestration live here. Recognition,
calibration, transport, and UI state are implemented in separate modules.
"""

import configparser
import gc
import os

from target_recognition import (
    AimReadyGate,
    ShotScoreSession,
    TargetObservation,
    TargetRecognizer,
)
from visual_calibration import VisualAimCalibration
from vision_config import VisionConfig
from vision_target_link import VisionTargetLink, build_status_payload
from vision_ui import (
    CONTROL_PREPARE,
    CONTROL_RUNNING,
    PAGE_CONTROL,
    PAGE_MAIN,
    PAGE_RESULT,
    PressReleaseLatch,
    VisionUiController,
    draw_control,
    draw_main,
    draw_result,
    draw_target_overlay,
    read_touch,
    settings_preview_frame,
)
from wav import RingVoiceReporter


class DetectionSnapshot:
    """Plain Python YOLO box safe to reuse without retaining native results."""

    def __init__(self, detection):
        def read(name, default):
            value = getattr(detection, name, default)
            return value() if callable(value) else value

        self.x = int(read("x", 0))
        self.y = int(read("y", 0))
        self.w = int(read("w", 0))
        self.h = int(read("h", 0))
        self.score = float(read("score", 0.0))

    @classmethod
    def from_box(cls, x, y, width, height, score=1.0):
        instance = cls.__new__(cls)
        instance.x = int(x)
        instance.y = int(y)
        instance.w = int(width)
        instance.h = int(height)
        instance.score = float(score)
        return instance


class RuntimeFaultGate:
    """Throttle repeated hardware errors so console I/O cannot starve frames."""

    def __init__(self, period_ms=2000):
        self.period_ms = max(100, int(period_ms))
        self.last_log_ms = {}
        self.counts = {}

    def report(self, stage, error, now_ms):
        stage = str(stage)
        self.counts[stage] = self.counts.get(stage, 0) + 1
        previous = self.last_log_ms.get(stage)
        if previous is not None and int(now_ms) - previous < self.period_ms:
            return False
        self.last_log_ms[stage] = int(now_ms)
        print("%s failed (%d): %s" % (stage, self.counts[stage], error))
        return True


def _recognition_required(controller):
    return (
        (controller.page == PAGE_CONTROL and controller.selected_task >= 2) or
        (controller.page == PAGE_MAIN and controller.settings_open)
    )


def _set_calibration_laser(link, controller, enabled, now_ms):
    """Request guarded SET laser state after revoking control coordinates."""
    link.send_vision(0, 0, False, False, False, 0, now_ms, force=True)
    command = "cal_laser_on" if enabled else "cal_laser_off"
    success = link.send_command(command)
    controller.settings_laser_requested = bool(enabled and success)
    return success


def _snapshot_detections(detections, limit):
    snapshots = []
    for index, detection in enumerate(detections or ()):
        if index >= max(1, int(limit)):
            break
        snapshots.append(DetectionSnapshot(detection))
    return snapshots


def _empty_observation(recognizer, now_ms):
    observation = TargetObservation()
    observation.timestamp_ms = int(now_ms)
    observation.move_epoch = recognizer.movement.epoch
    return observation


def _close_resource(resource):
    if resource is None:
        return
    for name in ("close", "deinit"):
        method = getattr(resource, name, None)
        if method is not None:
            try:
                method()
            except Exception:
                pass
            return


def _sleep_ms(time_module, delay_ms):
    try:
        time_module.sleep_ms(max(1, int(delay_ms)))
    except Exception:
        pass


def validate_model(mud_path):
    parser = configparser.ConfigParser(interpolation=None)
    try:
        if not parser.read(mud_path, encoding="utf-8"):
            return False
        if parser.get("basic", "type", fallback="").strip().lower() != "axmodel":
            print("model metadata must use [basic] type=axmodel")
            return False
        mud_dir = os.path.dirname(mud_path)
        references = []
        for key in ("model_npu", "model_vnpu", "model"):
            value = parser.get("basic", key, fallback="").strip()
            if value:
                references.append(value)
        if not references:
            return False
        for reference in references:
            path = reference if os.path.isabs(reference) else os.path.join(mud_dir, reference)
            if not os.path.isfile(path):
                print("model file missing:", path)
                return False
        return True
    except Exception as error:
        print("model validation failed:", error)
        return False


def apply_exposure(cam, camera_module, config):
    try:
        if config.manual_exposure:
            cam.exp_mode(camera_module.AeMode.Manual)
            cam.exposure(config.exposure)
        else:
            cam.exp_mode(camera_module.AeMode.Auto)
    except Exception as error:
        print("exposure setting failed:", error)


def _phase_allows_move_monitor(controller, status, completed_shots):
    if controller.control_state != CONTROL_RUNNING or controller.selected_task < 3:
        return False
    phase = str(status.phase).upper()
    if "MOVE" in phase or "WAIT" in phase:
        return True
    return completed_shots == 1 and not status.laser


VISION_LOSS_TRACK_CONTINUE = "TRACK_CONTINUE"
VISION_LOSS_WAIT_RECOVERY = "WAIT_RECOVERY"
VISION_LOSS_WAIT_RELOCK = "WAIT_RELOCK"


def _runtime_vision_loss_action(controller, status):
    """Describe MCU behavior after an invalid frame; never end the task here."""
    task_id = int(controller.selected_task)
    phase = str(status.phase).strip().upper()
    if task_id in (4, 5) and phase in ("TRACK_FINISH", "TRACK_TO_FINISH"):
        return VISION_LOSS_WAIT_RELOCK
    if phase in (
        "AIM_ACQUIRE", "LASER_DWELL", "WAIT_MOVE", "WAIT_TARGET_MOVE",
    ):
        return VISION_LOSS_WAIT_RECOVERY
    return VISION_LOSS_TRACK_CONTINUE


def _handle_runtime_vision_timeout(controller, status, link, now_ms, move_epoch=0):
    """Revoke aim data while leaving the MCU competition task active."""
    action = _runtime_vision_loss_action(controller, status)
    link.invalidate_vision(now_ms, move_epoch=move_epoch, force=True)
    if action == VISION_LOSS_WAIT_RELOCK:
        controller.notice = "VISION LOST: WAIT RELOCK"
    elif action == VISION_LOSS_WAIT_RECOVERY:
        controller.notice = "VISION LOST: WAIT RECOVERY"
    else:
        controller.notice = "VISION LOST: ROUTE CONTINUES"
    return action


def _handle_click(
    action, controller, config, calibration, observation, link, now_ms,
    apply_exposure_callback,
):
    if action is None:
        return
    if controller.page == PAGE_MAIN:
        if not controller.settings_open:
            if action.startswith("task"):
                controller.select_task(int(action[-1]))
            elif action == "set":
                controller.settings_open = True
                if _set_calibration_laser(
                    link, controller, True, now_ms,
                ):
                    controller.notice = "LASER REQUEST"
                else:
                    controller.notice = "UART ERROR"
        elif action == "set_cal":
            calibration.start()
            controller.notice = "CALIBRATING"
        elif action == "set_rst":
            calibration.reset()
            controller.notice = "CAL RESET"
        elif action == "item":
            controller.setting_index = (controller.setting_index + 1) % 3
        elif action in ("minus", "plus"):
            config.adjust_setting(controller.setting_index, 1 if action == "plus" else -1)
            link.telemetry_host = config.udp_host
            link.command_host = config.udp_host
            apply_exposure_callback()
        elif action == "back":
            _set_calibration_laser(link, controller, False, now_ms)
            controller.settings_open = False
            controller.notice = ""
        return

    if controller.page == PAGE_CONTROL:
        if controller.control_state == CONTROL_PREPARE:
            if action == "cal":
                calibration.start()
                controller.notice = "CALIBRATING"
            elif action == "rst":
                calibration.reset()
                controller.notice = "CAL RESET"
            elif action == "home":
                link.safe_stop(now_ms)
                controller.home()
            elif action == "start":
                if controller.start_running(
                    calibration.calibrated, observation.locked, link.available,
                ):
                    # Revoke stale coordinates before arming a new task.
                    link.send_vision(0, 0, False, False, False, 0, now_ms, force=True)
                    link.clear_task_status(controller.selected_task)
                    if not link.send_command("task%d" % controller.selected_task):
                        controller.control_state = CONTROL_PREPARE
                        controller.notice = "UART ERROR"
        elif action == "stop":
            link.safe_stop(now_ms)
            controller.finish(
                False, False, controller.shot_scores,
                link.task_status.milestones, link.combined_error,
            )
        return

    if action == "home":
        controller.home()
    elif action == "restart":
        controller.restart()


def main():
    from maix import app, camera, display, image, time, touchscreen

    config = VisionConfig()
    detector = None
    camera_width = config.camera_width
    camera_height = config.camera_height
    camera_format = image.Format.FMT_RGB888
    if config.enable_yolo:
        from maix import nn

        model_path = config.resolve_model_path(__file__)
        if model_path is None or not validate_model(model_path):
            print("MaixCAM2 model not found or invalid:", config.model_filename)
            return
        try:
            detector = nn.YOLO11(model=model_path, dual_buff=True)
        except Exception as error:
            print("YOLO11 load failed:", error)
            return
        camera_width = detector.input_width()
        camera_height = detector.input_height()
        camera_format = detector.input_format()

    cam = camera.Camera(camera_width, camera_height, camera_format)
    disp = display.Display()
    touch = touchscreen.TouchScreen()
    apply_exposure(cam, camera, config)

    link = VisionTargetLink(
        device=config.uart_device,
        baud=config.uart_baud,
        period_ms=config.target_send_period_ms,
        yaw_only=config.target_yaw_only,
        polarity_x=config.output_polarity_x,
        polarity_y=config.output_polarity_y,
        telemetry_host=config.udp_host,
        telemetry_port=config.telemetry_port,
        command_port=config.command_port,
        status_port=config.status_port,
        command_host=config.udp_host,
    )
    link.start()

    calibration = VisualAimCalibration(
        camera_width // 2,
        camera_height // 2,
        sample_frames=config.calibration_samples,
        max_spread_px=config.calibration_max_spread_px,
    )
    recognizer = TargetRecognizer(config)
    aim_gate = AimReadyGate(config.aim_stable_ms)
    ui = VisionUiController()
    touch_latch = PressReleaseLatch()
    shot_session = ShotScoreSession()
    voice_reporter = RingVoiceReporter()
    last_status_ms = -config.status_period_ms
    last_good_frame_ms = time.ticks_ms()
    frame_timeout_latched = False
    frame_count = 0
    recognition_frame_index = 0
    detection_snapshots = []
    faults = RuntimeFaultGate(config.error_log_period_ms)
    first_display_reported = False
    first_detector_reported = False
    first_target_reported = False
    last_display_frame = None
    last_display_ms = -config.display_period_ms
    display_retry_after_ms = 0

    print(
        "vision ready: stable detector=%s, quad scan=%d" % (
            "yolo" if config.enable_yolo else "red",
            int(config.enable_quad_scan),
        )
    )
    try:
        while not app.need_exit():
            now_ms = time.ticks_ms()
            link.poll()
            try:
                frame = cam.read()
            except Exception as error:
                faults.report("camera", error, now_ms)
                frame = None
            if frame is None:
                aim_gate.reset()
                if ui.settings_laser_requested:
                    _set_calibration_laser(link, ui, False, now_ms)
                    ui.notice = "LASER OFF: CAMERA"
                link.invalidate_vision(now_ms, force=False)
                running = (
                    ui.page == PAGE_CONTROL and
                    ui.control_state == CONTROL_RUNNING
                )
                if (
                    running and not frame_timeout_latched and
                    now_ms - last_good_frame_ms >= config.frame_timeout_ms
                ):
                    frame_timeout_latched = True
                    _handle_runtime_vision_timeout(
                        ui, link.task_status, link, now_ms,
                    )
                _sleep_ms(time, config.camera_retry_delay_ms)
                continue
            last_good_frame_ms = now_ms
            if frame_timeout_latched and ui.notice.startswith("VISION LOST:"):
                ui.notice = "VISION RECOVERED"
            frame_timeout_latched = False

            observation = _empty_observation(recognizer, now_ms)
            recognition_active = _recognition_required(ui)
            if recognition_active:
                recognition_frame_index += 1
                if config.enable_yolo:
                    detect_due = (
                        recognition_frame_index == 1 or
                        (recognition_frame_index - 1) %
                        max(1, config.detector_period_frames) == 0
                    )
                else:
                    detect_due = recognition_frame_index == 1
                if detect_due and config.enable_yolo:
                    try:
                        raw_detections = detector.detect(
                            frame,
                            conf_th=config.confidence,
                            iou_th=config.iou_threshold,
                        )
                        detection_snapshots = _snapshot_detections(
                            raw_detections, config.max_detection_candidates,
                        )
                        del raw_detections
                        if not first_detector_reported:
                            first_detector_reported = True
                            print("first detector frame ok: yolo")
                    except Exception as error:
                        detection_snapshots = []
                        faults.report("detect", error, now_ms)
                        if ui.settings_laser_requested:
                            _set_calibration_laser(link, ui, False, now_ms)
                            ui.notice = "LASER OFF: DETECT"
                elif not config.enable_yolo:
                    detection_snapshots = [DetectionSnapshot.from_box(
                        0, 0, frame.width(), frame.height(), 1.0,
                    )]
                    if not first_detector_reported:
                        first_detector_reported = True
                        print("first detector frame ok: red")
                move_monitor = _phase_allows_move_monitor(
                    ui, link.task_status, shot_session.completed,
                )
                try:
                    observation = recognizer.update(
                        frame,
                        detection_snapshots,
                        aim_point=(calibration.reference_x, calibration.reference_y),
                        now_ms=now_ms,
                        movement_enabled=move_monitor,
                    )
                    if not first_target_reported:
                        first_target_reported = True
                        print("first target frame ok")
                except Exception as error:
                    faults.report("recognition", error, now_ms)
                    if ui.settings_laser_requested:
                        _set_calibration_laser(link, ui, False, now_ms)
                        ui.notice = "LASER OFF: VISION"
                    try:
                        observation = recognizer.update(
                            None, [], now_ms=now_ms,
                            movement_enabled=move_monitor,
                        )
                    except Exception:
                        observation = _empty_observation(recognizer, now_ms)
            else:
                recognition_frame_index = 0
                detection_snapshots = []

            status = link.task_status
            dx = 0
            dy = 0
            try:
                calibration_complete = False
                if recognition_active:
                    calibration_complete = calibration.update(
                        observation.center if observation.locked else None
                    )
                if calibration_complete:
                    ui.notice = "CAL OK %d/%d" % (
                        calibration.reference_x, calibration.reference_y,
                    )
                elif ui.settings_open and calibration.pending:
                    current, total = calibration.progress
                    ui.notice = "CAL %d/%d %s" % (
                        current, total,
                        "LOCK" if observation.locked else "LOST",
                    )
                if observation.center is not None:
                    dx, dy = calibration.output_error(observation.center)

                running = (
                    ui.page == PAGE_CONTROL and
                    ui.control_state == CONTROL_RUNNING
                )
                aim_condition = (
                    running and ui.selected_task >= 2 and
                    calibration.calibrated and
                    observation.control_valid and
                    abs(dx) <= config.aim_deadband_x and
                    abs(dy) <= config.aim_deadband_y
                )
                aim_ready = aim_gate.update(aim_condition, now_ms)
                vision_valid = (
                    running and ui.selected_task >= 2 and
                    calibration.calibrated and
                    observation.control_valid
                )
                link.send_vision(
                    dx, dy, vision_valid, observation.locked, aim_ready,
                    observation.move_epoch, now_ms,
                )

                if running and shot_session.update(
                    status.laser, status.shot_index, observation.score, now_ms,
                ):
                    ui.shot_scores = list(shot_session.results)
                    voice_reporter.report(
                        shot_session.active_shot + 1,
                        shot_session.results[shot_session.active_shot],
                    )

                if running and status.done:
                    if status.laser:
                        shot_session.update(
                            False, status.shot_index,
                            observation.score, now_ms,
                        )
                        ui.shot_scores = list(shot_session.results)
                        voice_reporter.report(
                            shot_session.active_shot + 1,
                            shot_session.results[shot_session.active_shot],
                        )
                    link.invalidate_vision(
                        now_ms, move_epoch=observation.move_epoch, force=True,
                    )
                    ui.finish(
                        True, True, ui.shot_scores, status.milestones,
                        link.combined_error,
                    )

                touch_x, touch_y, pressed = read_touch(touch, frame, disp, image)
                touched = ui.button_at(
                    touch_x, touch_y, frame.width(), frame.height(),
                )
                clicked = touch_latch.update(pressed, touched)
                before_page = ui.page
                before_control_state = ui.control_state
                _handle_click(
                    clicked, ui, config, calibration, observation, link, now_ms,
                    lambda: apply_exposure(cam, camera, config),
                )
                entered_control_state = (
                    ui.page == PAGE_CONTROL and
                    (before_page != PAGE_CONTROL or
                     before_control_state != ui.control_state)
                )
                if entered_control_state:
                    shot_session.reset()
                    ui.shot_scores = [-1, -1]
                    aim_gate.reset()
                    if ui.control_state == CONTROL_PREPARE:
                        recognizer.reset_session()
                        voice_reporter.reset_session()
                        recognition_frame_index = 0
                        detection_snapshots = []

                if now_ms - last_status_ms >= config.status_period_ms:
                    last_status_ms = now_ms
                    phase = status.phase if status.valid else ui.control_state
                    payload = build_status_payload(
                        ui.selected_task, phase, status.shot_index,
                        observation.locked, observation.geometry, dx, dy,
                        observation.score, observation.quality,
                        status.milestones, now_ms,
                    )
                    link.send_status(payload)
            except Exception as error:
                faults.report("frame control", error, now_ms)
                aim_gate.reset()
                if ui.settings_laser_requested:
                    _set_calibration_laser(link, ui, False, now_ms)
                    ui.notice = "LASER OFF: CONTROL"
                link.invalidate_vision(
                    now_ms, move_epoch=observation.move_epoch, force=True,
                )

            display_due = (
                now_ms >= display_retry_after_ms and
                now_ms - last_display_ms >= config.display_period_ms
            )
            if display_due:
                display_frame = frame
                shown_frame = None
                try:
                    if ui.page == PAGE_MAIN:
                        display_frame = settings_preview_frame(frame, ui, config)
                        if (
                            ui.settings_open and ui.setting_index != 1 and
                            observation is not None
                        ):
                            draw_target_overlay(
                                display_frame, observation, calibration, image,
                            )
                        draw_main(
                            display_frame, ui, config,
                            link.available, link.udp_available,
                            image, touch_latch.armed,
                        )
                    elif ui.page == PAGE_CONTROL:
                        dwell_ms = shot_session.dwell_ms(now_ms)
                        draw_control(
                            display_frame, ui, observation, calibration, status,
                            dx, dy, observation.score, shot_session.completed,
                            dwell_ms, link.available, link.udp_available,
                            image, touch_latch.armed,
                        )
                    else:
                        draw_result(
                            display_frame, ui,
                            link.available, link.udp_available,
                            link.tx_count, link.rx_line_count,
                            image, touch_latch.armed,
                        )
                    disp.show(display_frame, fit=image.Fit.FIT_CONTAIN)
                    shown_frame = display_frame
                    last_display_ms = now_ms
                    if not first_display_reported:
                        first_display_reported = True
                        print("first display frame ok")
                except Exception as error:
                    faults.report("display", error, now_ms)
                    display_retry_after_ms = (
                        now_ms + config.display_error_cooldown_ms
                    )
                    if ui.settings_laser_requested:
                        _set_calibration_laser(link, ui, False, now_ms)
                        ui.notice = "LASER OFF: DISPLAY"
                finally:
                    # At 20 Hz the display has a full panel interval to consume
                    # the previous asynchronous submission before it is freed.
                    if shown_frame is not None:
                        last_display_frame = shown_frame

            frame_count += 1
            if frame_count % max(1, config.gc_period_frames) == 0:
                gc.collect()
    finally:
        try:
            _set_calibration_laser(link, ui, False, time.ticks_ms())
            link.safe_stop(time.ticks_ms())
        except Exception:
            pass
        voice_reporter.close()
        link.close()
        for resource in (touch, cam, disp, detector):
            _close_resource(resource)
        last_display_frame = None
        gc.collect()
        print("vision stopped")


if __name__ == "__main__":
    main()
