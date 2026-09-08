import serial
import time
import sys
from pathlib import Path

SERIAL_PORT = "/dev/pts/1"
BAUDRATE = 115200
KERNEL_PATH = Path("../vladBootin.img")

CHUNK_SIZE = 1
CHUNK_DELAY = 0.001


def send_data(ser, data, description="data"):
    total = len(data)
    sent = 0

    while sent < total:
        chunk = data[sent:sent + CHUNK_SIZE]

        written = ser.write(chunk)
        ser.flush()

        sent += written

        percent = (sent * 100) // total
        print(
            f"\rSending {description}: "
            f"{sent}/{total} bytes ({percent}%)",
            end="",
            flush=True,
        )

        if CHUNK_DELAY:
            time.sleep(CHUNK_DELAY)

    print()


def main():
    print("================================")
    print(" VladBootin Serial Image Loader")
    print("================================")

    if not KERNEL_PATH.exists():
        print(f"ERROR: kernel not found: {KERNEL_PATH}")
        sys.exit(1)

    kernel = KERNEL_PATH.read_bytes()
    size = len(kernel)

    print(f"Serial port : {SERIAL_PORT}")
    print(f"Baudrate    : {BAUDRATE}")
    print(f"Kernel      : {KERNEL_PATH}")
    print(f"Kernel size : {size} bytes")

    try:
        with serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUDRATE,
            timeout=1,
            write_timeout=5,
        ) as ser:

            # Give the serial device a moment to settle.
            time.sleep(0.2)

            # -------------------------------------------------
            # 1. SYN
            # -------------------------------------------------
            print("Sending SYN...")
            ser.write(bytes([0x16]))
            ser.flush()

            print("SYN sent.")

            # VladBootin historically waits after SYN.
            time.sleep(1)

            # -------------------------------------------------
            # 2. Kernel size
            # -------------------------------------------------
            size_bytes = size.to_bytes(4, byteorder="little")

            print(
                "Sending kernel size: "
                f"{size} bytes "
                f"({size_bytes.hex()})"
            )

            ser.write(size_bytes)
            ser.flush()

            time.sleep(1)

            # -------------------------------------------------
            # 3. Kernel
            # -------------------------------------------------
            print("Starting kernel transfer...")

            send_data(
                ser,
                kernel,
                description="kernel",
            )

            print("Kernel transfer complete.")

            # Give VladBootin time to receive the final bytes.
            time.sleep(1)

            print("Serial connection closed.")

    except serial.SerialTimeoutException:
        print("\nERROR: serial write timeout.")
        sys.exit(1)

    except serial.SerialException as e:
        print(f"\nERROR: serial port: {e}")
        sys.exit(1)

    except KeyboardInterrupt:
        print("\nTransfer interrupted by user.")
        sys.exit(130)


if __name__ == "__main__":
    main()

