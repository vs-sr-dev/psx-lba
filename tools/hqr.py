"""HQR archive reader for the DOS release of Little Big Adventure.

Format, as implemented by engine/LIB_SYS/HQ_R_M.C and EXPAND.C:

    u32                 size of the offset table, in bytes
    u32 * (size / 4)    absolute file offset of every entry
    per entry:
        u32 real_size
        u32 packed_size
        u16 method          0 = stored, 1 = LZSS
        u8  data[packed_size]

Entries can be empty (offset table holes); those come back as b"".

No game data ships with this repository. Point the tools at your own DOS
install -- in a GOG copy that is `Speedrun/Windows/`, NOT `Common/`, which
holds the 2023 remaster's archives under the same file names.
"""

from __future__ import annotations

import struct
from pathlib import Path


def expand(src: bytes, out_size: int) -> bytes:
    """LZSS decompressor, a transliteration of LIB_SYS/EXPAND.C.

    Control byte, LSB first: 1 = literal byte, 0 = a 16-bit back-reference
    where the low nibble is (length - 2) and the high 12 bits are
    (distance - 1) counted back from the current output position.
    """
    dst = bytearray(out_size)
    si = 0
    di = 0
    left = out_size

    while left > 0:
        ctrl = src[si]
        si += 1

        for _ in range(8):
            if ctrl & 1:
                dst[di] = src[si]
                si += 1
                di += 1
                left -= 1
                if left == 0:
                    return bytes(dst)
            else:
                token = src[si] | (src[si + 1] << 8)
                si += 2
                count = (token & 0x0F) + 2
                left -= count
                back = di - (token >> 4) - 1
                for _ in range(count):
                    dst[di] = dst[back]
                    di += 1
                    back += 1
                if left <= 0:
                    return bytes(dst)

            ctrl >>= 1

    return bytes(dst)


class Hqr:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.raw = self.path.read_bytes()
        table_bytes = struct.unpack_from("<I", self.raw, 0)[0]
        count = table_bytes // 4
        self.offsets = list(struct.unpack_from(f"<{count}I", self.raw, 0))

    def __len__(self) -> int:
        return len(self.offsets)

    def __getitem__(self, index: int) -> bytes:
        off = self.offsets[index]
        if off == 0 or off + 10 > len(self.raw):
            return b""
        real, packed, method = struct.unpack_from("<IIH", self.raw, off)
        body = self.raw[off + 10 : off + 10 + packed]
        if method == 0:
            return body[:real]
        if method == 1:
            return expand(body, real)
        raise ValueError(f"{self.path.name}[{index}]: unknown method {method}")

    def entries(self):
        for i in range(len(self)):
            yield i, self[i]
