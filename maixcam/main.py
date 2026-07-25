"""MaixVision entry point for target vision, debug display and ESP TCP handoff."""


def main():
    print("APP_MODE|BOX_FACE_WITH_TOF_TCP_DEBUG_DISPLAY", flush=True)
    from box_face_app import main as run_box_face_with_tof
    from vision_link import VisionTcpBridge

    bridge = VisionTcpBridge()
    try:
        run_box_face_with_tof(loop_hook=bridge.poll)
    finally:
        bridge.close()


if __name__ == "__main__":
    main()
