#!/usr/bin/env python3

import json
import math
import os
import select
import signal
import termios
import threading
import time
from typing import Callable, Optional

import rclpy
from drive_by_wire_chassis_bridge.msg import VehicleStatus
from geometry_msgs.msg import Twist
from rclpy.node import Node


BAUD_RATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
}


class SerialLinePort:
    def __init__(self, path: str, baudrate: int, on_line: Callable[[str], None]):
        self.path = path
        self.baudrate = baudrate
        self.on_line = on_line
        self.fd: Optional[int] = None
        self._reader_stop = threading.Event()
        self._reader: Optional[threading.Thread] = None
        self._write_lock = threading.Lock()

    @property
    def is_open(self) -> bool:
        return self.fd is not None

    def open(self) -> None:
        if self.fd is not None:
            return

        baud = BAUD_RATES.get(int(self.baudrate))
        if baud is None:
            raise ValueError(f"unsupported baudrate: {self.baudrate}")

        fd = os.open(self.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0
        attrs[4] = baud
        attrs[5] = baud
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIOFLUSH)

        self.fd = fd
        self._reader_stop.clear()
        self._reader = threading.Thread(target=self._read_loop, name="stm32-cdc-reader", daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._reader_stop.set()
        if self._reader is not None:
            self._reader.join(timeout=0.5)
            self._reader = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def write_json(self, payload: dict) -> None:
        line = json.dumps(payload, separators=(",", ":")).encode("ascii") + b"\n"
        self.write(line)

    def write(self, data: bytes) -> None:
        if self.fd is None:
            raise RuntimeError("serial port is not open")

        with self._write_lock:
            offset = 0
            deadline = time.monotonic() + 0.2
            while offset < len(data):
                if time.monotonic() > deadline:
                    raise TimeoutError("serial write timed out")
                _, writable, _ = select.select([], [self.fd], [], 0.05)
                if not writable:
                    continue
                offset += os.write(self.fd, data[offset:])

    def _read_loop(self) -> None:
        buffer = bytearray()
        while not self._reader_stop.is_set():
            fd = self.fd
            if fd is None:
                return
            readable, _, _ = select.select([fd], [], [], 0.1)
            if not readable:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            except OSError:
                return
            if not chunk:
                continue
            buffer.extend(chunk)
            while b"\n" in buffer:
                raw, _, rest = buffer.partition(b"\n")
                buffer = bytearray(rest)
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    self.on_line(line)


class CmdVelUsbBridge(Node):
    def __init__(self):
        super().__init__("cmd_vel_usb_bridge")

        self.declare_parameter("port", "/dev/ttyACM0")
        self.declare_parameter("baudrate", 115200)
        self.declare_parameter("cmd_vel_topic", "/cmd_vel")
        self.declare_parameter("vehicle_status_topic", "/vehicle_status")
        self.declare_parameter("vehicle_frame_id", "base_link")
        self.declare_parameter("publish_rate_hz", 20.0)
        self.declare_parameter("status_poll_rate_hz", 10.0)
        self.declare_parameter("cmd_vel_timeout_sec", 0.30)
        self.declare_parameter("wheel_diameter_m", 0.25)
        self.declare_parameter("drive_max_rpm", 150.0)
        self.declare_parameter("min_drive_rpm", 100.0)
        self.declare_parameter("linear_deadband_mps", 0.02)
        self.declare_parameter("use_status_drive_max_rpm", True)
        self.declare_parameter("max_angular_speed_radps", 1.0)
        self.declare_parameter("wheelbase_m", 1.72)
        self.declare_parameter("max_steering_angle_deg", 30.0)
        self.declare_parameter("min_yaw_conversion_speed_mps", 0.05)
        self.declare_parameter("steer_sign", 1.0)
        self.declare_parameter("throttle_deadband_axis", 20)
        self.declare_parameter("steer_deadband_axis", 5)
        self.declare_parameter("min_steer_axis", 80)
        self.declare_parameter("enable_remote_takeover", True)
        self.declare_parameter("set_monitor_on_shutdown", True)
        self.declare_parameter("send_neutral_when_stale", True)
        self.declare_parameter("log_stm32_replies", True)

        self.port_path = str(self.get_parameter("port").value)
        self.baudrate = int(self.get_parameter("baudrate").value)
        topic = str(self.get_parameter("cmd_vel_topic").value)
        status_topic = str(self.get_parameter("vehicle_status_topic").value)
        publish_rate_hz = float(self.get_parameter("publish_rate_hz").value)
        status_poll_rate_hz = float(self.get_parameter("status_poll_rate_hz").value)

        self.serial: Optional[SerialLinePort] = None
        self.seq = 0
        self.last_twist = Twist()
        self.last_twist_time: Optional[float] = None
        self.last_connect_attempt = 0.0
        self.connected_logged = False
        self.shutdown_started = False
        self.status_drive_max_rpm: Optional[float] = None
        self.last_cmd_gear = "N"
        self.last_cmd_throttle_axis = 0
        self.last_cmd_steer_axis = 0
        self.last_cmd_linear_x_mps = 0.0
        self.last_cmd_angular_z_radps = 0.0
        self.cmd_vel_stale = True

        self.create_subscription(Twist, topic, self._on_cmd_vel, 10)
        self.vehicle_status_pub = self.create_publisher(VehicleStatus, status_topic, 10)
        self.timer = self.create_timer(1.0 / max(1.0, publish_rate_hz), self._on_timer)
        self.status_timer = None
        if status_poll_rate_hz > 0.0:
            self.status_timer = self.create_timer(1.0 / status_poll_rate_hz, self._on_status_timer)
        self.get_logger().info(
            f"listening to {topic}, publishing {status_topic}, sending STM32 JSON over {self.port_path}"
        )

    def destroy_node(self):
        self.shutdown_started = True
        self._send_shutdown_commands()
        if self.serial is not None:
            self.serial.close()
        super().destroy_node()

    def _on_cmd_vel(self, msg: Twist) -> None:
        self.last_twist = msg
        self.last_twist_time = time.monotonic()

    def _on_timer(self) -> None:
        if self.shutdown_started:
            return
        if not self._ensure_connected():
            return

        payload = self._make_control_payload()
        if payload is None:
            return

        try:
            assert self.serial is not None
            self.serial.write_json(payload)
        except OSError as exc:
            self.get_logger().warn(f"serial write failed: {exc}")
            self._drop_serial()
        except Exception as exc:
            self.get_logger().warn(f"failed to send control frame: {exc}")

    def _on_status_timer(self) -> None:
        if self.shutdown_started:
            return
        if self.serial is None or not self.serial.is_open:
            return
        try:
            self.serial.write_json({"cmd": "get_status"})
        except OSError as exc:
            self.get_logger().warn(f"serial status request failed: {exc}")
            self._drop_serial()
        except Exception as exc:
            self.get_logger().warn(f"failed to request STM32 status: {exc}")

    def _ensure_connected(self) -> bool:
        if self.serial is not None and self.serial.is_open:
            return True

        now = time.monotonic()
        if (now - self.last_connect_attempt) < 1.0:
            return False
        self.last_connect_attempt = now

        try:
            serial_port = SerialLinePort(self.port_path, self.baudrate, self._on_serial_line)
            serial_port.open()
            self.serial = serial_port
            self.connected_logged = True
            self.get_logger().info(f"opened STM32 USB CDC port {self.port_path}")
            serial_port.write_json({"cmd": "get_status"})
            if bool(self.get_parameter("enable_remote_takeover").value):
                serial_port.write_json({"cmd": "set_remote_mode", "mode": "TAKEOVER"})
            return True
        except PermissionError:
            self.get_logger().error(
                f"permission denied opening {self.port_path}; add this user to dialout or fix udev permissions"
            )
        except FileNotFoundError:
            self.get_logger().warn(f"serial port not found: {self.port_path}")
        except Exception as exc:
            self.get_logger().warn(f"could not open {self.port_path}: {exc}")

        self._drop_serial()
        return False

    def _drop_serial(self) -> None:
        if self.serial is not None:
            try:
                self.serial.close()
            except Exception:
                pass
        self.serial = None

    def _make_control_payload(self) -> Optional[dict]:
        now = time.monotonic()
        timeout = float(self.get_parameter("cmd_vel_timeout_sec").value)
        stale = self.last_twist_time is None or (now - self.last_twist_time) > timeout

        if stale:
            if not bool(self.get_parameter("send_neutral_when_stale").value):
                return None
            linear_x = 0.0
            angular_z = 0.0
        else:
            linear_x = float(self.last_twist.linear.x)
            angular_z = float(self.last_twist.angular.z)

        throttle = self._throttle_axis(linear_x)
        steer = self._steer_axis(linear_x, angular_z)
        if abs(throttle) < int(self.get_parameter("throttle_deadband_axis").value):
            throttle = 0
        if abs(steer) < int(self.get_parameter("steer_deadband_axis").value):
            steer = 0
        elif 0 < abs(steer) < int(self.get_parameter("min_steer_axis").value):
            steer = int(math.copysign(int(self.get_parameter("min_steer_axis").value), steer))

        gear = "N"
        if throttle > 0:
            gear = "D"
        elif throttle < 0:
            gear = "R"

        self.last_cmd_gear = gear
        self.last_cmd_throttle_axis = throttle
        self.last_cmd_steer_axis = steer
        self.last_cmd_linear_x_mps = linear_x
        self.last_cmd_angular_z_radps = angular_z
        self.cmd_vel_stale = stale

        self.seq = (self.seq + 1) & 0xFFFFFFFF
        if self.seq == 0:
            self.seq = 1

        return {
            "cmd": "ros_control",
            "seq": self.seq,
            "gear": gear,
            "throttle": throttle,
            "steer": steer,
            "aux_x": 0,
            "aux_y": 0,
            "buttons": 0,
        }

    def _throttle_axis(self, linear_x: float) -> int:
        linear_deadband = max(0.0, float(self.get_parameter("linear_deadband_mps").value))
        if abs(linear_x) <= linear_deadband:
            return 0

        wheel_diameter = max(0.001, float(self.get_parameter("wheel_diameter_m").value))
        wheel_circumference = math.pi * wheel_diameter
        target_rpm = (linear_x / wheel_circumference) * 60.0

        min_drive_rpm = max(0.0, float(self.get_parameter("min_drive_rpm").value))
        if 0.0 < abs(target_rpm) < min_drive_rpm:
            target_rpm = math.copysign(min_drive_rpm, target_rpm)

        throttle = target_rpm / self._drive_max_rpm() * 1000.0
        return int(round(self._clamp(throttle, -1000.0, 1000.0)))

    def _drive_max_rpm(self) -> float:
        if (
            bool(self.get_parameter("use_status_drive_max_rpm").value)
            and self.status_drive_max_rpm is not None
            and self.status_drive_max_rpm > 0.0
        ):
            return self.status_drive_max_rpm
        return max(1.0, float(self.get_parameter("drive_max_rpm").value))

    def _steer_axis(self, linear_x: float, angular_z: float) -> int:
        wheelbase = max(0.001, float(self.get_parameter("wheelbase_m").value))
        max_steer_angle = math.radians(max(0.1, float(self.get_parameter("max_steering_angle_deg").value)))
        min_speed = max(0.0, float(self.get_parameter("min_yaw_conversion_speed_mps").value))
        max_angular = max(0.001, float(self.get_parameter("max_angular_speed_radps").value))
        steer_sign = float(self.get_parameter("steer_sign").value)

        if abs(linear_x) >= min_speed:
            target_angle = math.atan(wheelbase * angular_z / linear_x)
            normalized = self._clamp(target_angle / max_steer_angle, -1.0, 1.0)
        else:
            normalized = self._clamp(angular_z / max_angular, -1.0, 1.0)

        return int(round(steer_sign * normalized * 1000.0))

    def _on_serial_line(self, line: str) -> None:
        should_log = bool(self.get_parameter("log_stm32_replies").value)
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            if should_log:
                self.get_logger().debug(f"STM32: {line}")
            return

        cmd = data.get("cmd", "")
        ok = data.get("ok", None)
        if cmd in ("status_ack", "config_ack", "chassis_status"):
            self._update_status_drive_max_rpm(data)
            if cmd in ("status_ack", "chassis_status"):
                self._publish_vehicle_status(data, line)
            if should_log and ok is False:
                self.get_logger().warn(f"STM32 {cmd}: {line}")
            return
        if not should_log:
            return
        if ok is False:
            self.get_logger().warn(f"STM32: {line}")
        else:
            self.get_logger().info(f"STM32: {line}")

    def _publish_vehicle_status(self, data: dict, raw_line: str) -> None:
        msg = VehicleStatus()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = str(self.get_parameter("vehicle_frame_id").value)

        msg.ok = self._bool_value(data, "ok")
        msg.firmware = str(data.get("fw", ""))
        msg.chassis_type = str(data.get("ct", ""))
        msg.drive_axle = str(data.get("da", ""))
        msg.drive_mode = str(data.get("dm", ""))
        msg.drive_max_rpm = self._float_value(data, "dmr")
        msg.steering_max_rpm = self._float_value(data, "smr")

        msg.gear = str(data.get("g", ""))
        msg.control_owner = str(data.get("co", ""))
        msg.remote_mode = str(data.get("rm", ""))
        msg.remote_takeover = self._bool_value(data, "rto")
        msg.local_control_active = self._bool_value(data, "lca")
        msg.vehicle_moving = self._bool_value(data, "mov")
        msg.vehicle_moving_command = self._bool_value(data, "mvc")
        msg.vehicle_moving_actual = self._bool_value(data, "mva")
        msg.control_enabled = self._bool_value(data, "oe")
        msg.outputs_locked_by_fault = self._bool_value(data, "olf")
        msg.soft_stop_active = self._bool_value(data, "ssa")
        msg.emergency_stop_active = self._bool_value(data, "esa")
        msg.hardware_estop_active = self._bool_value(data, "hea")
        msg.fault_code = str(data.get("fc", ""))
        msg.fault_domain = str(data.get("fd", ""))
        msg.drive_can_fault = self._bool_value(data, "dcf")
        msg.steer_can_fault = self._bool_value(data, "scf")
        msg.handwheel_can_fault = self._bool_value(data, "hcf")
        msg.can_recovery_active = self._bool_value(data, "cra")
        msg.linear_steering_enabled = self._bool_value(data, "ls")
        msg.linear_steering_detected = self._bool_value(data, "lsd")

        msg.throttle_raw = self._float_value(data, "thr")
        msg.brake_raw = self._float_value(data, "brk")
        msg.left_target_rpm = self._float_value(data, "lr")
        msg.right_target_rpm = self._float_value(data, "rr")
        msg.left_wheel_rpm = self._float_value(data, "lar")
        msg.right_wheel_rpm = self._float_value(data, "rar")
        msg.left_drive_valid = self._bool_value(data, "lav")
        msg.right_drive_valid = self._bool_value(data, "rav")
        msg.drive_feedback_valid = self._bool_value(data, "dfv")
        msg.linear_speed_mps = self._linear_speed_from_feedback(msg.left_wheel_rpm, msg.right_wheel_rpm, msg.gear)

        msg.steering_input = self._int_value(data, "si")
        msg.steering_rpm_cmd = self._int_value(data, "sr")
        msg.steering_target_raw = self._int_value(data, "str")
        msg.steering_target_angle_deg = self._int_value(data, "sta")
        msg.steering_actual_angle_deg = self._int_value(data, "saa")
        msg.steering_feedback_raw = self._int_value(data, "sf")
        msg.steering_engaged = self._bool_value(data, "seg")
        msg.steering_feedback_valid = self._bool_value(data, "sfv")
        msg.steering_feedback_age_ms = self._uint_value(data, "sfa")

        msg.drive_can_ready = self._bool_value(data, "dc")
        msg.steer_can_ready = self._bool_value(data, "sc")
        msg.stm32_tx_count = self._uint_value(data, "stx")
        msg.stm32_rx_count = self._uint_value(data, "srx")
        msg.steer_can_timeout_count = self._uint_value(data, "stmo")
        msg.steer_can_queue_depth = self._uint_value(data, "sq")
        msg.steer_can_read_pending = self._bool_value(data, "sp")
        msg.steer_can_node_id = self._uint_value(data, "sid")
        msg.handwheel_can_node_id = self._uint_value(data, "hid")
        msg.left_drive_inverted = self._bool_value(data, "ldi")
        msg.right_drive_inverted = self._bool_value(data, "rdi")

        msg.remote_valid = self._bool_value(data, "rv")
        msg.remote_age_ms = self._uint_value(data, "ra")
        msg.remote_source = str(data.get("rs", ""))
        msg.remote_frame_count = self._uint_value(data, "rfc")
        msg.remote_parse_errors = self._uint_value(data, "rpe")
        msg.remote_overrun_errors = self._uint_value(data, "roe")
        msg.ble_connected = self._bool_value(data, "ble_connected")

        msg.last_cmd_gear = self.last_cmd_gear
        msg.last_cmd_throttle_axis = self.last_cmd_throttle_axis
        msg.last_cmd_steer_axis = self.last_cmd_steer_axis
        msg.last_cmd_linear_x_mps = self.last_cmd_linear_x_mps
        msg.last_cmd_angular_z_radps = self.last_cmd_angular_z_radps
        msg.cmd_vel_stale = self.cmd_vel_stale
        msg.raw_json = raw_line

        self.vehicle_status_pub.publish(msg)

    def _linear_speed_from_feedback(self, left_rpm: float, right_rpm: float, gear: str) -> float:
        wheel_diameter = max(0.001, float(self.get_parameter("wheel_diameter_m").value))
        linear_speed = ((left_rpm + right_rpm) * 0.5) * (math.pi * wheel_diameter) / 60.0
        if gear == "R" and linear_speed > 0.0:
            return -linear_speed
        return linear_speed

    @staticmethod
    def _float_value(data: dict, key: str, default: float = 0.0) -> float:
        try:
            return float(data.get(key, default))
        except (TypeError, ValueError):
            return default

    @staticmethod
    def _int_value(data: dict, key: str, default: int = 0) -> int:
        try:
            return int(data.get(key, default))
        except (TypeError, ValueError):
            return default

    @classmethod
    def _uint_value(cls, data: dict, key: str, default: int = 0) -> int:
        return max(0, cls._int_value(data, key, default))

    @staticmethod
    def _bool_value(data: dict, key: str) -> bool:
        value = data.get(key, False)
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, float)):
            return value != 0
        if isinstance(value, str):
            return value.strip().lower() in ("1", "true", "yes", "on")
        return bool(value)

    def _update_status_drive_max_rpm(self, data: dict) -> None:
        raw_value = data.get("dmr", data.get("drive_max_rpm"))
        if raw_value is None:
            return
        try:
            value = float(raw_value)
        except (TypeError, ValueError):
            return
        if value <= 0.0:
            return
        if self.status_drive_max_rpm != value:
            self.status_drive_max_rpm = value
            wheel_diameter = max(0.001, float(self.get_parameter("wheel_diameter_m").value))
            max_speed = (math.pi * wheel_diameter * value) / 60.0
            self.get_logger().info(
                f"STM32 drive_max_rpm={value:.0f}; wheel_diameter={wheel_diameter:.3f}m; "
                f"max_linear_speed={max_speed:.3f}m/s"
            )

    def _send_shutdown_commands(self) -> None:
        if self.serial is None or not self.serial.is_open:
            return
        try:
            self.seq = (self.seq + 1) & 0xFFFFFFFF
            self.serial.write_json(
                {
                    "cmd": "ros_control",
                    "seq": self.seq or 1,
                    "gear": "N",
                    "throttle": 0,
                    "steer": 0,
                    "aux_x": 0,
                    "aux_y": 0,
                    "buttons": 0,
                }
            )
            if bool(self.get_parameter("set_monitor_on_shutdown").value):
                self.serial.write_json({"cmd": "set_remote_mode", "mode": "MONITOR"})
        except Exception as exc:
            self.get_logger().warn(f"failed to send shutdown neutral frame: {exc}")

    @staticmethod
    def _clamp(value: float, low: float, high: float) -> float:
        return min(max(value, low), high)


def main(args=None):
    rclpy.init(args=args)
    node = CmdVelUsbBridge()

    def _handle_signal(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, _handle_signal)

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
