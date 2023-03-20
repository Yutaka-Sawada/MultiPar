import os
import sys
import json

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

# Construct path of JSON file
save_path = os.path.dirname(sys.argv[0])
json_path = os.path.join(save_path, os.path.basename(recovery_path)) + ".json"
if os.path.isfile(json_path):
    print("JSON file =\n" + json_path)

    # Open the JSON file and read the contents.
    with open(json_path, 'r', encoding='utf-8') as f:
        json_dict = json.load(f)

        # Get directory of recovery files.
        file_path = json_dict["SelectedFile"]
        recv_dir = os.path.dirname(file_path)
        print("\nRecovery files' directory = " + recv_dir)

        # Get list of recovery files.
        recv_list = json_dict["RecoveryFile"]
        for file_name in recv_list:
            print(file_name)

        # Get directory of source files.
        src_dir = json_dict["BaseDirectory"]
        print("\nSource files' directory = " + src_dir)

        # Get list of source files.
        src_list = json_dict["SourceFile"]
        for file_name in src_list:
            print(file_name)

    # Erase JSON file (If you want to keep JSON file, comment out next line.)
    os.remove(json_path)

# If you don't confirm result, comment out below line.
input('Press [Enter] key to continue . . .')
