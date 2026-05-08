import re
import sys


LINE_RE = re.compile(r"ELRS/CRSF TX len=26 crc=1 bytes=([0-9A-F ]+)")


def unpack_channels(frame_bytes):
    payload = frame_bytes[3:25]
    channels = []
    bitbuf = 0
    bits = 0
    idx = 0
    while len(channels) < 16:
        while bits < 11 and idx < len(payload):
            bitbuf |= payload[idx] << bits
            bits += 8
            idx += 1
        channels.append(bitbuf & 0x07FF)
        bitbuf >>= 11
        bits -= 11
    return channels


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python tools/decode_crsf_rc_log.py <log_path>")

    with open(sys.argv[1], encoding="ascii", errors="ignore") as handle:
        for line in handle:
            match = LINE_RE.search(line)
            if not match:
                continue
            frame = bytes(int(value, 16) for value in match.group(1).split())
            if len(frame) != 26 or frame[2] != 0x16:
                continue
            channels = unpack_channels(frame)
            print(f"CH1={channels[0]} CH2={channels[1]} CH3={channels[2]} CH4={channels[3]}")


if __name__ == "__main__":
    main()
