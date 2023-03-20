import sys
import os
import subprocess


# Set path of MultiPar
client_path = "../MultiPar.exe"

# Set path of file-list
list_path = "../save/file-list.txt"


# Make file-list of source files in a folder
# Return number of found files
def make_list(path):
    f = open(list_path, 'w', encoding='utf-8')
    file_count = 0

    # Search inner files
    with os.scandir(path) as it:
        for entry in it:
            # Exclude sub-folder and short-cut
            if entry.is_file() and (not entry.name.endswith('lnk')):
                #print("file name=", entry.name)
                #print("file size=", entry.stat().st_size)

                # Check file size and ignore small files
                # Set the limit number (bytes) on below line.
                if entry.stat().st_size >= 1048576:
                    f.write(entry.name)
                    f.write('\n')
                    file_count += 1

    # Finish file-list
    f.close()
    return file_count


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
    # You must check MultiPar Option: "Always use folder name for base filename".
    if os.path.exists(par_path):
        print(one_name + " includes PAR file already.")
        continue

    # Make file-list
    file_count = make_list(one_path)
    #print("file_count=", file_count)
    if file_count > 0:

        # Set command-line
        # Cover path by " for possible space
        # Specify source file by file-list
        # The file-list will be deleted by MultiPar automatically.
        cmd = "\"" + client_path + "\" /create /base \"" + one_path + "\" /list \"" + list_path + "\""

        # Process the command
        print("Creating PAR files.")
        error_level = command(cmd)

        # Check error
        # Exit loop, when error occur.
        if error_level > 0:
            print("Error=", error_level)
            break

    else:
        print(one_name + " doesn't contain source files.")
        os.remove(list_path)

# If you don't confirm result, comment out below line.
input('Press [Enter] key to continue . . .')
