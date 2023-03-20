import sys
import os
import subprocess


# Set path of par2j
client_path = "../par2j64.exe"

# Set path of file-list
list_path = "../save/file-list.txt"


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

    # Check the PAR file exists already
    par_path = one_path + "\\#1.par2"
    if os.path.exists(par_path):
        print(one_name + " includes PAR files already.")
        continue

    # Create PAR file for each 1000 source files.
    group_index = 1
    file_count = 0
    error_level = 0

    # Set options for par2j
    par_option = "/rr10 /rd2"

    # Make file-list of source files in a folder
    f = open(list_path, 'w', encoding='utf-8')

    # Search inner directories and files
    for cur_dir, dirs, files in os.walk(one_path):
        for file_name in files:
            # Ignore existing PAR2 files
            if file_name.lower().endswith('.par2'):
                continue

            # Save filename and sub-directory
            file_path = os.path.join(os.path.relpath(cur_dir, one_path), file_name)
            if file_path.startswith('.\\'):
                file_path = os.path.basename(file_path)

            #print("file name=", file_path)
            f.write(file_path)
            f.write('\n')
            file_count += 1

            # If number of source files reaches 1000, create PAR file for them.
            if file_count >= 1000:
                f.close()
                #print("file_count=", file_count)
                par_path = one_path + "\\#" + str(group_index) + ".par2"

                # Set command-line
                # Cover path by " for possible space
                # Specify source file by file-list
                cmd = "\"" + client_path + "\" c " + par_option + " /d\"" + one_path + "\" /fu \"" + par_path + "\" \"" + list_path + "\""

                # Process the command
                print("Creating PAR files for group:", group_index)
                error_level = command(cmd)

                # Check error
                # Exit loop, when error occur.
                if error_level > 0:
                    print("Error=", error_level)
                    break

                # Set for next group
                group_index += 1
                file_count = 0
                f = open(list_path, 'w', encoding='utf-8')

    # Exit loop, when error occur.
    if error_level > 0:
        break

    # Finish file-list
    f.close()

    # If there are source files still, create the last PAR file.
    #print("file_count=", file_count)
    if file_count > 0:
        par_path = one_path + "\\#" + str(group_index) + ".par2"
        cmd = "\"" + client_path + "\" c " + par_option + " /d\"" + one_path + "\" /fu \"" + par_path + "\" \"" + list_path + "\""

        # Process the command
        print("Creating PAR files for group:", group_index)
        error_level = command(cmd)

        # Check error
        # Exit loop, when error occur.
        if error_level > 0:
            print("Error=", error_level)
            break

    elif group_index == 1:
        print(one_name + " doesn't contain source files.")

    # Delete file-list after creation
    if (group_index > 1) or (file_count > 0):
        os.remove(list_path)

# If you don't confirm result, comment out below line.
input('Press [Enter] key to continue . . .')
