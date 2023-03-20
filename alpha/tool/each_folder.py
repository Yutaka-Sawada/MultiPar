import sys
import os
import subprocess

# This script is based on a script provided by Project Maintainer: Yukata-Sawada. I (markxu98) modified it for my own use. I tested it only on my machines
# with only my own files. Although I believe the structure of my files should reflex many different types of users/organizations, I cannot guarantee this
# script is suitable for ALL. So I suggest use it, understand it, and modify it by yourself to fit.

# The purpose of this script is to generate PAR2 files with (somewhat) "best" storage efficiency with (somewhat) "good" reliability, while spending
# (somewhat) "less" time or using (somewhat) "less" processing power.

# The following "const" values are optimized by me. They should be okay to most cases. Use caution when changing all of them. I added more comments to
# certain ones important to calculation.

# Set path of par2j
client_path = "../par2j64.exe"

# Set options for par2j
# Don't use /ss, /sn, /sr, or /sm here.
cmd_option = "/rr10 /rd1 /rf3"

# How to set slices initially (either /ss, /sn, or /sr)
# The default setting is /sr10 in MultiPar.
init_slice_option = "/sr10"

# Slice size multiplier (used in all cmd in this script)
# It would be good to set a cluster size of HDD/SSD. (4k == 4096 by default)
slice_size_multiplier = 4096

# Max number of slices at searching good efficiency (20000 by default).
# More number of slices is good for many files or varied size.
# This value can be ignored if initial slice count is even larger.
# Set this value to 32768 to ignore it completely.
max_slice_count = 20000

# Max percent of slices at searching good efficiency (170% by default).
# If initial_count is less than max_slice_count, the maximum slice count at
# searching is the smaller value of max_slice_count and
# (initial_count * max_slice_rate / 100).
# If initial_count is not less than max_slice_count, max_slice_count is
# ignored, and the maximum slice count at searching is the smaller value of
# 32768 and (initial_count * max_slice_rate / 100).
# This will also be used to calculate the min_slice_size.
max_slice_rate = 170
#max_slice_rate = 0
# If you have good processing power, you can set above rate to 0, then BOTH
# max_slice_count and max_slice_rate will be ignored. 32768 will be fixed as
# max_slice_count at searching.

# Min number of slices at searching good efficiency (100 by default).
# Normally less number of slices tend to archive higher efficiency.
# But, too few slices is bad against random error.
# If by very small chance, the calculated maximum slice count is less than
# min_slice_count, no searching is needed and min_slice_count is used to
# generate PAR2 file.
min_slice_count = 100

# Min percent of slices at searching good efficiency (30% by default).
# Normally less number of slices tend to archive higher efficiency.
# But, too few slices is bad against random error. It may need at least 50%.
# If initial_count is more than min_slice_count, the minimum slice count at
# searching is the greater value of min_slice_count and
# (initial_count * min_slice_rate / 100).
# If initial_count is not more than min_slice_count, min_slice_rate is
# ignored, and the minimum slice count at searching is min_slice_count.
# This will also be used to calculate the max_slice_size.
min_slice_rate = 30
#min_slice_rate = 0
# If you have good processing power, you can set above rate to 0, then this
# rate will be ignored and min_slice_count is used at searching

# Caution: You CAN set max_slice_rate = 0 and min_slice_rate = 0 at the same
# time to search (almost) "WHOLE" range, from min_slice_count to 32768.

# Min efficiency improvment that will be regarded as "better" (0.3% by default).
# If the efficiency improvement is not so significant, it's unreasonable to
# use a larger slice count. This value controls how significant to update the
# best slice count at searching.
min_efficiency_improvement = 0.3
#min_efficiency_improvement = 0
# If you want to achieve "absolute" best efficiency, you can set above to 0.

# Read "Efficiency rate"
def read_efficiency(output_text):
    # Find from the last
    line_start = output_text.rfind("Efficiency rate\t\t:")
    if line_start != -1:
        line_start += 19
        line_end = output_text.find("%\n", line_start)
        #print("line_start=", line_start)
        #print("line_end=", line_end)
        return float(output_text[line_start:line_end])
    else:
        return -1


# Read "Input File Slice count"
def read_slice_count(output_text):
    # Find from the top
    line_start = output_text.find("Input File Slice count\t:")
    if line_start != -1:
        line_start += 25
        line_end = output_text.find("\n", line_start)
        return int(output_text[line_start:line_end])
    else:
        return -1


# Read "Input File Slice size"
def read_slice_size(output_text):
    # Find from the top
    line_start = output_text.find("Input File Slice size\t:")
    if line_start != -1:
        line_start += 24
        line_end = output_text.find("\n", line_start)
        return int(output_text[line_start:line_end])
    else:
        return -1


# Search setting of good efficiency
def test_efficiency(par_path):
    min_size = 0
    max_size = 0
    best_count = 0
    best_size = 0
    best_efficiency = 0
    best_efficiency_at_initial_count = 0
    best_count_at_max_count = 0
    best_size_at_max_count = 0
    best_efficiency_at_max_count = 0

    # First time to get initial value
    cmd = "\"" + client_path + "\" t /uo " + init_slice_option + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" *"
    res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
    #print("return code: {}".format(res.returncode))
    #print("captured stdout: {}".format(res.stdout))
    if res.returncode != 0:
        return 0
    efficiency_rate = read_efficiency(res.stdout)
    if efficiency_rate < 0:
        return 0
    # DON'T change best_count, best_size and best_efficiency here. The following three values will be evaluated after the search is done.
    # Using initial_count may not be best case. If the search can find a slice count less than initial_count, whose efficiency is the same as
    # best_efficiency_at_initial_count, that slice count should be used instead of initial_count.
    initial_count = read_slice_count(res.stdout)
    if initial_count <= 0:
        return 0
    initial_size = read_slice_size(res.stdout)
    best_efficiency_at_initial_count = efficiency_rate
    #print("initial_size =", initial_size, ", initial_count =", initial_count, ", efficiency =", efficiency_rate)

    # Get min and max of slice count and size to be used at searching
    # maximum slice count is co-related to minimum slice size
    if max_slice_rate != 0:
        if initial_count > max_slice_count:
            if (initial_count * max_slice_rate / 100) > 32768:
                max_count = 32768
            else:
                max_count = int(initial_count * max_slice_rate / 100)
        else:
            if (initial_count * max_slice_rate / 100) > max_slice_count:
                max_count = max_slice_count
            else:
                max_count = int(initial_count * max_slice_rate / 100)
    else:
        max_count = 32768
    # Giving out the calculated maximum slice count, get "real" max_count and min_size from result of Par2j64.exe
    # Here use option "/sn" to search around (from -12.5% to +6.25%) the calculated maximum slice count for best efficiency
    cmd = "\"" + client_path + "\" t /uo /sn" + str(max_count) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" *"
    res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
    if res.returncode != 0:
        return 0
    efficiency_rate = read_efficiency(res.stdout)
    if efficiency_rate < 0:
        return 0
    # DON'T change best_count, best_size and best_efficiency here. The following three values will be evaluated after the search is done.
    # Using max_count is the worst case as it will require more processing power. If the search can find a slice count less than max_count,
    # whose efficiency is the same as best_efficiency_at_max_count, that slice count should be used instead of best_count_at_max_count.
    best_count_at_max_count = read_slice_count(res.stdout)
    best_size_at_max_count = read_slice_size(res.stdout)
    best_efficiency_at_max_count = efficiency_rate
    max_count = read_slice_count(res.stdout)
    min_size = read_slice_size(res.stdout)
    #print("max_count =", max_count, ", min_size =", min_size, ", efficiency =", best_efficiency_at_max_count)

    # Minimum slice count is co-related to maximum slice size
    if min_slice_rate > 0 and (initial_count * min_slice_rate / 100) > min_slice_count:
        min_count = int(initial_count * min_slice_rate / 100)
    else:
        min_count = min_slice_count
    # Giving out the calculated minimum slice count, get "real" min_count and max_size from result of Par2j64.exe
    # Here use option "/sn" to search around (from -12.5% to +6.25%) the calculated minimum slice count for best efficiency
    cmd = "\"" + client_path + "\" t /uo /sn" + str(min_count) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" *"
    res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
    if res.returncode != 0:
        return 0
    efficiency_rate = read_efficiency(res.stdout)
    if efficiency_rate < 0:
        return 0
    min_count = read_slice_count(res.stdout)
    max_size = read_slice_size(res.stdout)
    best_count = read_slice_count(res.stdout)
    best_size = read_slice_size(res.stdout)
    best_efficiency = efficiency_rate
    #print("min_count =", min_count, ", max_size =", max_size, ", efficiency =", best_efficiency)

    # If the calculated maximum slice count is too small, no need to search (QUITE UNLIKELY to happen)
    if max_slice_rate > 0 and (initial_count * max_slice_rate / 100) <= min_slice_count:
        # Giving out min_slice_count, get "real" best_size from result of Par2j64.exe
        # Here use option "/sn" to search around (from -12.5% to +6.25%) the minimum slice count for best efficiency
        cmd = "\"" + client_path + "\" t /uo /sn" + str(min_slice_count) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" *"
        res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
        if res.returncode != 0:
            return 0
        efficiency_rate = read_efficiency(res.stdout)
        if efficiency_rate < 0:
            return 0
        best_count = read_slice_count(res.stdout)
        best_size = read_slice_size(res.stdout)
        best_efficiency = efficiency_rate
        #print("initial_count too small, best_count =", best_count, ", best_size =", best_size, ", best_efficiency =", best_efficiency)
        # Return slice size to archive the best efficiency
        return best_size
    else:
        # Try every (step) slice count between min_count and max_count
        step_slice_count_int = int((min_count + 1) * 8 / 7)
        while step_slice_count_int < max_count:
            #print(f"Testing slice count: (around) {step_slice_count_int}, from {(step_slice_count_int - int(step_slice_count_int / 8))} to {int(step_slice_count_int * 17 / 16)}")
            # Giving out the calculated step slice count, get "real" slice count and size from result of Par2j64.exe
            # Here use option "/sn" to search around (from -12.5% to +6.25%) the calculated step slice count for best efficiency
            cmd = "\"" + client_path + "\" t /uo /sn" + str(step_slice_count_int) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" *"
            res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
            if res.returncode != 0:
                break
            efficiency_rate = read_efficiency(res.stdout)
            if efficiency_rate < 0:
                break
            if efficiency_rate > best_efficiency + min_efficiency_improvement:
                best_count = read_slice_count(res.stdout)
                best_size = read_slice_size(res.stdout)
                best_efficiency = efficiency_rate
            # Next count should be more than 17/16 of the input count. (Range to +6.25% was checked already.)
            step_slice_count_int = int((int(step_slice_count_int * 17 / 16) + 1) * 8 / 7)
        # Evaluate slice count searched with initial_count
        if initial_count < best_count and best_efficiency_at_initial_count > best_efficiency - min_efficiency_improvement:
            best_count = initial_count
            best_size = initial_size
            best_efficiency = best_efficiency_at_initial_count
        # Evaluate slice count searched with max_count.
        if best_efficiency_at_max_count > best_efficiency + min_efficiency_improvement:
            best_count = best_count_at_max_count
            best_size = best_size_at_max_count
            best_efficiency = best_efficiency_at_max_count
        #print("best_count =", best_count, "best_size =", best_size, ", best_efficiency =", best_efficiency)

    return best_size


# Return sub-process's ExitCode
def command(cmd):
    ret = subprocess.run(cmd, shell=True)
    return ret.returncode


# Return zero for empty folder
def check_empty(path='.'):
    total = 0
    with os.scandir(path) as it:
        for entry in it:
            if entry.is_file():
                total += entry.stat().st_size
            elif entry.is_dir():
                total += check_empty(entry.path)
            if total > 0:
                break
    return total


# Read arguments of command-line
for idx, arg in enumerate(sys.argv[1:]):
    one_path = arg
    one_name = os.path.basename(one_path)

    # Check the folder exists
    if os.path.isdir(one_path) == False:
        print(one_name + " isn't folder.")
        continue

    # Check empty folder
    if check_empty(one_path) == 0:
        print(one_name + " is empty folder.")
        continue

    print(one_name + " is folder.")

    # Path of creating PAR file
    par_path = one_path + "\\" + one_name + ".par2"

    # Check the PAR file exists already
    if os.path.exists(par_path):
        print(one_name + " includes PAR file already.")
        continue

    # Test setting for good efficiency
    slice_size = test_efficiency(par_path)
    if slice_size == 0:
        print("Failed to test options.")
        continue

    # Set command-line
    # Cover path by " for possible space
    cmd = "\"" + client_path + "\" c /ss" + str(slice_size) + " " + cmd_option + " \"" + par_path + "\" *"
    # If you want to see creating result only, use "t" command instead of "c".

    # Process the command
    print("Creating PAR files.")
    error_level = command(cmd)

    # Check error
    # Exit loop, when error occur.
    if error_level > 0:
        print("Error=", error_level)
        break

# If you don't confirm result, comment out below line.
input('Press [Enter] key to continue . . .')
