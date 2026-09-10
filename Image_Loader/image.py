import serial
import sys
import time
from pathlib import Path


SERIAL_PORT = "/dev/pts/4"
BAUDRATE = 1500000

KERNEL_PATH = Path("kernel.img")
DTB_PATH = Path("bcm2709-rpi-2-b.dtb")

SYN = 0x16
ACK = 0x06
NAK = 0x15

CHUNK_SIZE = 256

ACK_TIMEOUT = 2.0



def wait_ack(ser, description="ACK"):
    response = ser.read(1)

    if not response:
        raise RuntimeError(f"Timeout waiting for {description}")

    if response[0] == ACK:
        return

    if response[0] == NAK:
        raise RuntimeError(
            f"VladBootin returned NAK while waiting for {description}"
        )

    raise RuntimeError(
        f"Unexpected response while waiting for {description}: "
        f"0x{response[0]:02x}"
    )


def send_size(ser, size, description):
    size_bytes = size.to_bytes(4, byteorder="little")

    print(
        f"Sending {description} size: "
        f"{size} bytes ({size_bytes.hex()})"
    )

    ser.write(size_bytes)
    ser.flush()

    wait_ack(ser, f"{description} size ACK")


def send_file(ser, data, description):
    total = len(data)
    sent = 0

    print(f"Starting {description} transfer...")

    while sent < total:
        chunk = data[sent:sent + CHUNK_SIZE]

        ser.write(chunk)
        ser.flush()

        wait_ack(ser, f"{description} chunk ACK")

        sent += len(chunk)

        percent = (sent * 100) // total

        print(
            f"\rSending {description}: "
            f"{sent}/{total} bytes ({percent}%)",
            end="",
            flush=True
        )

    print()


def serial_monitor(ser):
    print()
    print("================================")
    print(" Linux serial output")
    print(" Press Ctrl+C to exit")
    print("================================")
    print()

    ser.timeout = 0.1

    try:
        while True:
            waiting = ser.in_waiting

            if waiting:
                data = ser.read(waiting)

                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()

    except KeyboardInterrupt:
        print("\n\nSerial monitor terminated.")


def main():
    print("================================")
    print(" VladBootin Linux Serial Loader")
    print("================================")

    if not KERNEL_PATH.exists():
        print(f"ERROR: kernel not found: {KERNEL_PATH}")
        sys.exit(1)

    if not DTB_PATH.exists():
        print(f"ERROR: DTB not found: {DTB_PATH}")
        sys.exit(1)

    kernel = KERNEL_PATH.read_bytes()
    dtb = DTB_PATH.read_bytes()

    print(f"Serial port : {SERIAL_PORT}")
    print(f"Baudrate    : {BAUDRATE}")
    print(f"Kernel      : {KERNEL_PATH}")
    print(f"Kernel size : {len(kernel)} bytes")
    print(f"DTB         : {DTB_PATH}")
    print(f"DTB size    : {len(dtb)} bytes")

    try:
        with serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUDRATE,
            timeout=ACK_TIMEOUT,
            write_timeout=ACK_TIMEOUT,
            rtscts=False,
            dsrdtr=False,
        ) as ser:

            # Give the hardware UART/reset sequence time to settle.
            time.sleep(0.5)

            # Discard anything left over from startup.
            ser.reset_input_buffer()

            # -----------------------------------------------------
            # SYN
            # -----------------------------------------------------

            print("Sending SYN...")

            ser.write(bytes([SYN]))
            ser.flush()

            wait_ack(ser, "SYN ACK")

            print("SYN ACK received.")

            # -----------------------------------------------------
            # KERNEL
            # -----------------------------------------------------

            send_size(
                ser,
                len(kernel),
                "kernel"
            )

            send_file(
                ser,
                kernel,
                "kernel"
            )

            print("Kernel transfer complete.")

            # -----------------------------------------------------
            # DTB
            # -----------------------------------------------------

            send_size(
                ser,
                len(dtb),
                "DTB"
            )

            send_file(
                ser,
                dtb,
                "DTB"
            )

            print("DTB transfer complete.")

            print()
            print("================================")
            print("Kernel + DTB transferred.")
            print("Waiting for Linux...")
            print("================================")

            # -----------------------------------------------------
            # SERIAL MONITOR
            # -----------------------------------------------------

            serial_monitor(ser)

    except serial.SerialTimeoutException:
        print("\nERROR: serial write timeout.")
        sys.exit(1)

    except serial.SerialException as e:
        print(f"\nERROR: serial port: {e}")
        sys.exit(1)

    except RuntimeError as e:
        print(f"\nERROR: {e}")
        sys.exit(1)

    except KeyboardInterrupt:
        print("\nTransfer interrupted by user.")
        sys.exit(130)


if __name__ == "__main__":
    main()
