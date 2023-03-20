import os
import sys

os.system("TITLE After verification")

# Read command-line arguments
if len(sys.argv) > 0:
    this_path = sys.argv[0]
    print("Path of this script file =\n" + this_path + "\n")

if len(sys.argv) > 1:
    recovery_path = sys.argv[1]
    print("Path of a recovery file =\n" + recovery_path + "\n")

if len(sys.argv) > 2:
    base_path = sys.argv[2]
    print("Path of base directory of source files =\n" + base_path + "\n")

if len(sys.argv) > 3:
    exit_code = sys.argv[3]
    print("Status of source files = " + exit_code + "\n")

# If you don't confirm result, comment out below line.
input('Press [Enter] key to continue . . .')
