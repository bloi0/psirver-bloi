import sys
import time
print("**** Hello, world! ****")
for a in sys.argv:
    print("---", a)
print("No errors!", file=sys.stderr)
print("Clean output", file=sys.stderr)
sys.exit(24)
