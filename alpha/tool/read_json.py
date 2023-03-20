import sys
import os
import json

# Get path of JSON file from command-line.
json_path = sys.argv[1]
print("JSON file = " + json_path)

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

    # Get list of found source files.
    if "FoundFile" in json_dict:
        find_list = json_dict["FoundFile"]
        print("\nFound files =")
        for file_name in find_list:
            print(file_name)

    # Get list of external source files.
    if "ExternalFile" in json_dict:
        ext_list = json_dict["ExternalFile"]
        print("\nExternal files =")
        for file_name in ext_list:
            print(file_name)

    # Get list of damaged source files.
    if "DamagedFile" in json_dict:
        damage_list = json_dict["DamagedFile"]
        print("\nDamaged files =")
        for file_name in damage_list:
            print(file_name)

    # Get list of appended source files.
    if "AppendedFile" in json_dict:
        append_list = json_dict["AppendedFile"]
        print("\nAppended files =")
        for file_name in append_list:
            print(file_name)

    # Get list of missing source files.
    if "MissingFile" in json_dict:
        miss_list = json_dict["MissingFile"]
        print("\nMissing files =")
        for file_name in miss_list:
            print(file_name)

    # Get dict of misnamed source files.
    if "MisnamedFile" in json_dict:
        misname_dict = json_dict["MisnamedFile"]
        print("\nMisnamed files =")
        for file_names in misname_dict.items():
            print(file_names[0] + ", wrong name = " + file_names[1])

# If you don't confirm result, comment out below line.
input('Press [Enter] key to continue . . .')
