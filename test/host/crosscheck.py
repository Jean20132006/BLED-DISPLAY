#!/usr/bin/env python3
"""Cross-checks the C CRC-16 and calendar code against Python's independent implementations."""
import binascii, calendar, datetime, random, string, subprocess, sys
random.seed(7)
alpha = string.ascii_letters + string.digits + ",.-_+:"
bodies = ["PAD,3,41.2", "STAT,0,1,0,87", "123456789", "PING"] + ["".join(random.choice(alpha) for _ in range(random.randint(1, 90))) for _ in range(500)]
got = subprocess.run(["./test_proto", "crc"], input="\n".join(bodies) + "\n", capture_output=True, text=True).stdout.split()
bad = [b for b, r in zip(bodies, got) if r != "%04X" % binascii.crc_hqx(b.encode(), 0xFFFF)]
print(f"CRC-16: {len(bodies)} bodies vs binascii.crc_hqx, mismatches: {len(bad)}")
rows, exp = [], []
for _ in range(5000):
    y = random.randint(2000, 2099); m = random.randint(1, 12); d = random.randint(1, calendar.monthrange(y, m)[1])
    H, M, S = random.randint(0, 23), random.randint(0, 59), random.randint(0, 59)
    rows.append(f"{y-2000} {m} {d} {H} {M} {S}")
    exp.append(int((datetime.datetime(y, m, d, H, M, S) - datetime.datetime(2000, 1, 1)).total_seconds()))
out = subprocess.run(["./test_time", "x"], input="\n".join(rows), capture_output=True, text=True).stdout.split()
bad2 = sum(1 for o, e in zip(out, exp) if int(o) != e)
print(f"Calendar: 5000 timestamps vs datetime, mismatches: {bad2}")
sys.exit(1 if bad or bad2 else 0)
