import sys
import os
import subprocess
import stat
import tkinter as tk
from tkinter import ttk
from tkinter import filedialog


# Set path of MultiPar
client_path = "../par2j64.exe"
gui_path = "../MultiPar.exe"

# Set options for par2j
# Because /fe option is set to exclude .PAR2 files by default, no need to set here.
# Don't use /ss, /sn, /sr, or /sm here.
cmd_option = "/rr10 /rd2"

# How to set slices initially (either /ss, /sn, or /sr)
init_slice_option = "/sr10"

# Optimization settings. Refer "each_folder.py" for detail.
slice_size_multiplier = 4096
max_slice_count = 20000
max_slice_rate = 170
min_slice_count = 100
min_slice_rate = 30
min_efficiency_improvement = 0.3

# Initialize global variables
current_dir = "./"
sub_proc = None


# Read "Efficiency rate"
def read_efficiency(output_text):
    # Find from the last
    line_start = output_text.rfind("Efficiency rate\t\t:")
    if line_start != -1:
        line_start += 19
        line_end = output_text.find("%\n", line_start)
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
def test_efficiency(par_path, source_path):
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
    cmd = "\"" + client_path + "\" t /uo /fe\"**.par2\" " + init_slice_option + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" \"" + source_path + "\""
    res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
    if res.returncode != 0:
        return 0
    efficiency_rate = read_efficiency(res.stdout)
    if efficiency_rate < 0:
        return 0
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
    cmd = "\"" + client_path + "\" t /uo /fe\"**.par2\" /sn" + str(max_count) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" \"" + source_path + "\""
    res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
    if res.returncode != 0:
        return 0
    efficiency_rate = read_efficiency(res.stdout)
    if efficiency_rate < 0:
        return 0
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
    cmd = "\"" + client_path + "\" t /uo /fe\"**.par2\" /sn" + str(min_count) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" \"" + source_path + "\""
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
        cmd = "\"" + client_path + "\" t /uo /fe\"**.par2\" /sn" + str(min_slice_count) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" \"" + source_path + "\""
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
            cmd = "\"" + client_path + "\" t /uo /fe\"**.par2\" /sn" + str(step_slice_count_int) + " /sm" + str(slice_size_multiplier) + " " + cmd_option + " \"" + par_path + "\" \"" + source_path + "\""
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


# Return zero for empty folder
def check_empty(path='.'):
    total = 0
    with os.scandir(path) as it:
        for entry in it:
            if entry.is_file():
                # Ignore hidden file
                if entry.stat().st_file_attributes & stat.FILE_ATTRIBUTE_HIDDEN:
                    continue
                # Ignore PAR file
                entry_ext = os.path.splitext(entry.name)[1]
                if entry_ext.lower() == ".par2":
                    continue
                total += entry.stat().st_size
            elif entry.is_dir():
                total += check_empty(entry.path)
            if total > 0:
                break
    return total


# Search children folders or files in a parent folder
def search_child_item(parent_path):
    parent_name = os.path.basename(parent_path)

    # Check the folder exists already
    item_count = listbox_list1.size()
    item_path = parent_path + "\\"
    item_index = 0
    while item_index < item_count:
        index_path = listbox_list1.get(item_index)
        if os.path.samefile(item_path, index_path):
            label_status.config(text= "The folder \"" + parent_name + "\" is selected already.")
            return
        common_path = os.path.commonpath([item_path, index_path]) + "\\"
        if os.path.samefile(common_path, item_path):
            label_status.config(text= "The folder \"" + parent_name + "\" is parent of another selected item.")
            return
        if os.path.samefile(common_path, index_path):
            label_status.config(text= "The folder \"" + parent_name + "\" is child of another selected item.")
            return
        item_index += 1

    # Add found items
    error_text = ""
    add_count = 0
    for item_name in os.listdir(parent_path):
        # Ignore PAR files (extension ".par2")
        item_ext = os.path.splitext(item_name)[1]
        if item_ext.lower() == ".par2":
            error_text += " PAR file \"" + item_name + "\" is ignored."
            continue

        # Ignore hidden item
        item_path = os.path.join(parent_path, item_name)
        if os.stat(item_path).st_file_attributes & stat.FILE_ATTRIBUTE_HIDDEN:
            error_text += " Hidden \"" + item_name + "\" is ignored."
            continue

        # Distinguish folder or file
        if os.path.isdir(item_path):
            # Ignore empty folder
            if check_empty(item_path) == 0:
                continue
            item_path += "\\"

        listbox_list1.insert(tk.END, item_path)
        add_count += 1

    item_count = listbox_list1.size()
    label_head1.config(text= str(item_count) + " items")
    if item_count == 0:
        label_status.config(text= "There are no items." + error_text)
        button_start.config(state=tk.DISABLED)
    elif add_count == 0:
        label_status.config(text= "No items were found in folder \"" + parent_name + "\"." + error_text)
    else:
        label_status.config(text= str(add_count) + " items were found in folder \"" + parent_name + "\"." + error_text)
        button_reset.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_remove.config(state=tk.NORMAL)


# Select a folder to search children folders or files
def button_parent_clicked():
    global current_dir
    if os.path.exists(current_dir) == False:
        current_dir = "./"
    search_dir = filedialog.askdirectory(initialdir=current_dir)
    if search_dir == "":
        return
    current_dir = search_dir
    search_child_item(search_dir)


# Reset lists and display status
def button_reset_clicked():
    global current_dir
    current_dir = "./"

    # Clear list-box at first
    listbox_list1.delete(0, tk.END)
    listbox_list2.delete(0, tk.END)

    # Reset statues text
    label_head1.config(text= '0 items')
    label_head2.config(text= '0 finished items')
    label_status.config(text= 'Select folders and/or files to create PAR files.')

    # Reset button state
    button_parent.config(state=tk.NORMAL)
    button_child.config(state=tk.NORMAL)
    button_file.config(state=tk.NORMAL)
    button_reset.config(state=tk.DISABLED)
    button_start.config(state=tk.DISABLED)
    button_stop.config(state=tk.DISABLED)
    button_remove.config(state=tk.DISABLED)
    button_open2.config(state=tk.DISABLED)


# Check and add items
def add_argv_item():
    # Add specified items
    error_text = ""
    for one_path in sys.argv[1:]:
        # Make sure to be absolute path
        item_path = os.path.abspath(one_path)
        if os.path.exists(item_path) == False:
            error_text += " \"" + item_path + "\" doesn't exist."
            continue
        #print(item_path)

        # Ignore PAR files (extension ".par2")
        item_name = os.path.basename(item_path)
        item_ext = os.path.splitext(item_name)[1]
        if item_ext.lower() == ".par2":
            error_text += " PAR file \"" + item_name + "\" is ignored."
            continue

        # Ignore hidden item
        if os.stat(item_path).st_file_attributes & stat.FILE_ATTRIBUTE_HIDDEN:
            error_text += " Hidden \"" + item_name + "\" is ignored."
            continue

        # Distinguish folder or file
        if os.path.isdir(item_path):
            # Ignore empty folder
            if check_empty(item_path) == 0:
                continue
            item_path += "\\"

        # Check the item exists already or duplicates
        item_count = listbox_list1.size()
        item_index = 0
        while item_index < item_count:
            index_path = listbox_list1.get(item_index)
            if os.path.samefile(item_path, index_path):
                error_text += " \"" + item_name + "\" is selected already."
                item_count = -1
                break
            common_path = os.path.commonpath([item_path, index_path]) + "\\"
            if os.path.samefile(common_path, item_path):
                error_text += " \"" + item_name + "\" is parent of another selected item."
                item_count = -1
                break
            if os.path.samefile(common_path, index_path):
                error_text += " \"" + item_name + "\" is child of another selected item."
                item_count = -1
                break
            item_index += 1
        if item_count < 0:
            continue

        listbox_list1.insert(tk.END, item_path)

    item_count = listbox_list1.size()
    label_head1.config(text= str(item_count) + " items")
    if item_count == 0:
        label_status.config(text= "There are no items." + error_text)
    else:
        label_status.config(text= str(item_count) + " items were selected at first." + error_text)
        button_reset.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_remove.config(state=tk.NORMAL)


# Cretae the first PAR set
def queue_run():
    global sub_proc

    if sub_proc != None:
        return

    if "disabled" in button_stop.state():
        button_parent.config(state=tk.NORMAL)
        button_child.config(state=tk.NORMAL)
        button_file.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_remove.config(state=tk.NORMAL)
        button_open2.config(state=tk.NORMAL)
        label_status.config(text= "Stopped queue")
        return

    item_path = listbox_list1.get(0)
    if item_path == "":
        label_status.config(text= "There are no items.")
        return

    # When it's a folder, create PAR2 files for inner files.
    if item_path[-1:] == "\\":
        base_name = os.path.basename(item_path[:-1]) + ".par2"
        source_path = item_path + "*"
        par_path = item_path + base_name
    # When it's a file, create PAR2 files for the file.
    else:
        base_name = os.path.basename(item_path) + ".par2"
        source_path = item_path
        par_path = item_path + ".par2"
    label_status.config(text= "Creating \"" + base_name + "\"")

    # Test setting for good efficiency
    slice_size = test_efficiency(par_path, source_path)
    if slice_size == 0:
        label_status.config(text= "Failed to test options.")
        return

    # Set command-line
    # Cover path by " for possible space   
    cmd = "\"" + client_path + "\" c /fe\"**.par2\" /ss" + str(slice_size) + " " + cmd_option + " \"" + par_path + "\" \"" + source_path + "\""
    # If you want to see creating result only, use "t" command instead of "c".
    #print(cmd)

    # Run PAR2 client
    sub_proc = subprocess.Popen(cmd, shell=True)

    # Wait finish of creation
    root.after(300, queue_result)


# Wait and read created result
def queue_result():
    global sub_proc

    # When sub-process was not started yet
    if sub_proc == None:
        return

    # When sub-process is running still
    exit_code = sub_proc.poll()
    if exit_code == None:
        # Call self again
        root.after(300, queue_result)
        return

    sub_proc = None
    item_path = listbox_list1.get(0)

    # When fatal error happened in par2j
    if exit_code == 1:
        button_parent.config(state=tk.NORMAL)
        button_child.config(state=tk.NORMAL)
        button_file.config(state=tk.NORMAL)
        button_reset.config(state=tk.NORMAL)
        button_stop.config(state=tk.DISABLED)
        button_remove.config(state=tk.NORMAL)
        button_open2.config(state=tk.NORMAL)
        label_status.config(text= "Failed queue")
        return

    # When you cancel par2j on Command Prompt
    elif exit_code == 2:
        button_parent.config(state=tk.NORMAL)
        button_child.config(state=tk.NORMAL)
        button_file.config(state=tk.NORMAL)
        button_reset.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_stop.config(state=tk.DISABLED)
        button_remove.config(state=tk.NORMAL)
        button_open2.config(state=tk.NORMAL)
        label_status.config(text= "Canceled queue")
        return

    # When par files were created successfully
    else:
        #print("exit code =", exit_code)
        # Add to list of finished items
        listbox_list2.insert(tk.END, item_path)
        item_count = listbox_list2.size()
        label_head2.config(text= str(item_count) + " finished items")

    # Remove the first item from the list
    listbox_list1.delete(0)

    # Process next set
    item_count = listbox_list1.size()
    if item_count == 0:
        button_parent.config(state=tk.NORMAL)
        button_child.config(state=tk.NORMAL)
        button_file.config(state=tk.NORMAL)
        button_reset.config(state=tk.NORMAL)
        button_stop.config(state=tk.DISABLED)
        button_open2.config(state=tk.NORMAL)
        label_status.config(text= "Created all items")

    elif "disabled" in button_stop.state():
        button_parent.config(state=tk.NORMAL)
        button_child.config(state=tk.NORMAL)
        button_file.config(state=tk.NORMAL)
        button_reset.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_remove.config(state=tk.NORMAL)
        button_open2.config(state=tk.NORMAL)
        label_status.config(text= "Interrupted queue")

    else:
        root.after(100, queue_run)


# Select a child folder to add
def button_child_clicked():
    global current_dir
    if os.path.exists(current_dir) == False:
        current_dir = "./"
    one_path = filedialog.askdirectory(initialdir=current_dir)
    if one_path == "":
        return
    current_dir = os.path.dirname(one_path)

    # Check the folder has content
    one_name = os.path.basename(one_path)
    if check_empty(one_path) == 0:
        label_status.config(text= "Selected folder \"" + one_name + "\" is empty.")
        return

    # Check the folder is new
    one_path += "\\"
    item_count = listbox_list1.size()
    item_index = 0
    while item_index < item_count:
        index_path = listbox_list1.get(item_index)
        if os.path.samefile(one_path, index_path):
            label_status.config(text= "Folder \"" + one_name + "\" is selected already.")
            return
        common_path = os.path.commonpath([one_path, index_path]) + "\\"
        if os.path.samefile(common_path, one_path):
            label_status.config(text= "Folder \"" + one_name + "\" is parent of another selected item.")
            return
        if os.path.samefile(common_path, index_path):
            label_status.config(text= "Folder \"" + one_name + "\" is child of another selected item.")
            return
        item_index += 1
    listbox_list1.insert(tk.END, one_path)
    item_count += 1

    label_head1.config(text= str(item_count) + " items")
    label_status.config(text= "Folder \"" + one_name + "\" was added.")
    button_reset.config(state=tk.NORMAL)
    button_start.config(state=tk.NORMAL)
    button_remove.config(state=tk.NORMAL)


# Select multiple children files to add
def button_file_clicked():
    global current_dir
    if os.path.exists(current_dir) == False:
        current_dir = "./"
    multi_path = filedialog.askopenfilenames(initialdir=current_dir)
    if len(multi_path) == 0:
        return
    one_path = multi_path[0]
    current_dir = os.path.dirname(one_path)

    # Add selected items
    error_text = ""
    add_count = 0
    for one_path in multi_path:
        # Ignore PAR file (extension ".par2")
        one_name = os.path.basename(one_path)
        item_ext = os.path.splitext(one_name)[1]
        if item_ext.lower() == ".par2":
            error_text += " PAR file \"" + one_name + "\" is ignored."
            continue

        # Ignore hidden file
        if os.stat(one_path).st_file_attributes & stat.FILE_ATTRIBUTE_HIDDEN:
            error_text += " Hidden \"" + one_name + "\" is ignored."
            continue

        # Check the file is new
        item_count = listbox_list1.size()
        item_index = 0
        while item_index < item_count:
            index_path = listbox_list1.get(item_index)
            if os.path.samefile(one_path, index_path):
                error_text += " \"" + one_name + "\" is selected already."
                item_count = -1
                break
            common_path = os.path.commonpath([one_path, index_path]) + "\\"
            if os.path.samefile(common_path, index_path):
                error_text += " \"" + one_name + "\" is child of another selected item."
                item_count = -1
                break
            item_index += 1
        if item_count < 0:
            continue

        add_name = one_name
        listbox_list1.insert(tk.END, one_path)
        add_count += 1

    item_count = listbox_list1.size()
    label_head1.config(text= str(item_count) + " items")
    if item_count == 0:
        label_status.config(text= "There are no items." + error_text)
        button_start.config(state=tk.DISABLED)
    elif add_count == 0:
        label_status.config(text= "No files were added." + error_text)
    else:
        if add_count == 1:
            label_status.config(text= "File \"" + add_name + "\" was added." + error_text)
        else:
            label_status.config(text= str(add_count) + " files were added." + error_text)
        button_reset.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_remove.config(state=tk.NORMAL)


# Select a child file to add
def button_file1_clicked():
    global current_dir
    if os.path.exists(current_dir) == False:
        current_dir = "./"
    one_path = filedialog.askopenfilename(initialdir=current_dir)
    if one_path == "":
        return
    current_dir = os.path.dirname(one_path)

    # Ignore PAR file (extension ".par2")
    one_name = os.path.basename(one_path)
    item_ext = os.path.splitext(one_name)[1]
    # Compare filename in case insensitive
    item_ext = item_ext.lower()
    if item_ext == ".par2":
        label_status.config(text= "PAR file \"" + one_name + "\" is ignored.")
        return

    # Ignore hidden file
    if os.stat(one_path).st_file_attributes & stat.FILE_ATTRIBUTE_HIDDEN:
        label_status.config(text= "Hidden \"" + one_name + "\" is ignored.")
        return

    # Check the file is new
    item_count = listbox_list1.size()
    item_index = 0
    while item_index < item_count:
        index_path = listbox_list1.get(item_index)
        if os.path.samefile(one_path, index_path):
            label_status.config(text= "File \"" + one_name + "\" is selected already.")
            return
        common_path = os.path.commonpath([one_path, index_path]) + "\\"
        if os.path.samefile(common_path, index_path):
            label_status.config(text= "File \"" + one_name + "\" is child of another selected item.")
            return
        item_index += 1

    listbox_list1.insert(tk.END, one_path)
    item_count += 1

    label_head1.config(text= str(item_count) + " items")
    label_status.config(text= "File \"" + one_name + "\" was added.")
    button_reset.config(state=tk.NORMAL)
    button_start.config(state=tk.NORMAL)
    button_remove.config(state=tk.NORMAL)


# Resume stopped queue
def button_start_clicked():
    global sub_proc

    item_count = listbox_list1.size()
    if item_count == 0:
        label_status.config(text= "There are no items.")
        return

    button_parent.config(state=tk.DISABLED)
    button_child.config(state=tk.DISABLED)
    button_file.config(state=tk.DISABLED)
    button_reset.config(state=tk.DISABLED)
    button_start.config(state=tk.DISABLED)
    button_stop.config(state=tk.NORMAL)
    button_remove.config(state=tk.DISABLED)
    button_open2.config(state=tk.DISABLED)

    if sub_proc == None:
        queue_run()
    else:
        queue_result()


# Stop running queue
def button_stop_clicked():
    button_stop.config(state=tk.DISABLED)
    if sub_proc != None:
        label_status.config(text= "Waiting finish of current task")


# Remove items from list
def button_remove_clicked():
    # It's possible to select multiple items.
    selected_indices = listbox_list1.curselection()
    selected_count = len(selected_indices)
    if selected_count == 0:
        label_status.config(text= "Select items to remove at first.")
        return

    label_status.config(text= "Removed " + str(selected_count) + " items.")
    while selected_count > 0:
        selected_count -= 1
        selected_index = selected_indices[selected_count]
        # Remove selected items at once
        listbox_list1.delete(selected_index)

    item_count = listbox_list1.size()
    label_head1.config(text= str(item_count) + " items")
    if item_count == 0:
        button_start.config(state=tk.DISABLED)
        button_remove.config(state=tk.DISABLED)


# Open a PAR set by MultiPar
def button_open2_clicked():
    if os.path.exists(gui_path) == False:
        label_status.config(text= "Cannot call \"" + gui_path + "\". Set path correctly.")
        return

    indices = listbox_list2.curselection()
    if len(indices) == 1:
        item_path = listbox_list2.get(indices[0])
        if item_path[-1:] == "\\":
            base_name = os.path.basename(item_path[:-1]) + ".par2"
            par_path = item_path + base_name
        else:
            base_name = os.path.basename(item_path) + ".par2"
            par_path = item_path + ".par2"
        label_status.config(text= "Opening \"" + base_name + "\"")

        # Set command-line
        # Cover path by " for possible space   
        cmd = "\"" + gui_path + "\" /verify \"" + par_path + "\""

        # Open MultiPar GUI to see details
        # Because this doesn't wait finish of MultiPar, you may open some at once.
        subprocess.Popen(cmd)

    else:
        label_status.config(text= "Select one item to open at first.")


# Window size and title
root = tk.Tk()
root.title('PAR Queue - Create')
root.minsize(width=480, height=200)
# Centering window
init_width = 640
init_height = 480
init_left = (root.winfo_screenwidth() - init_width) // 2
init_top = (root.winfo_screenheight() - init_height) // 2
root.geometry('{}x{}+{}+{}'.format(init_width, init_height, init_left, init_top))
#root.geometry("640x480")
root.columnconfigure(0, weight=1)
root.rowconfigure(1, weight=1)

# Control panel
frame_top = ttk.Frame(root, padding=(3,4,3,2))
frame_top.grid(row=0, column=0, sticky=(tk.E,tk.W))

button_parent = ttk.Button(frame_top, text="Search inner folder", width=18, command=button_parent_clicked)
button_parent.pack(side=tk.LEFT, padx=2)

button_child = ttk.Button(frame_top, text="Add single folder", width=16, command=button_child_clicked)
button_child.pack(side=tk.LEFT, padx=2)

button_file = ttk.Button(frame_top, text="Add multi files", width=14, command=button_file_clicked)
button_file.pack(side=tk.LEFT, padx=2)

button_reset = ttk.Button(frame_top, text="Reset lists", width=11, command=button_reset_clicked, state=tk.DISABLED)
button_reset.pack(side=tk.LEFT, padx=2)

# List
frame_middle = ttk.Frame(root, padding=(2,2,2,2))
frame_middle.grid(row=1, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_middle.rowconfigure(0, weight=1)
frame_middle.columnconfigure(0, weight=1)
frame_middle.columnconfigure(1, weight=1)

# List of children items (folders and files)
frame_list1 = ttk.Frame(frame_middle, padding=(6,2,6,6), relief='groove')
frame_list1.grid(row=0, column=0, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list1.columnconfigure(0, weight=1)
frame_list1.rowconfigure(2, weight=1)

frame_top1 = ttk.Frame(frame_list1, padding=(0,4,0,3))
frame_top1.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

button_start = ttk.Button(frame_top1, text="Start", width=6, command=button_start_clicked, state=tk.DISABLED)
button_start.pack(side=tk.LEFT, padx=2)

button_stop = ttk.Button(frame_top1, text="Stop", width=6, command=button_stop_clicked, state=tk.DISABLED)
button_stop.pack(side=tk.LEFT, padx=2)

button_remove = ttk.Button(frame_top1, text="Remove", width=8, command=button_remove_clicked, state=tk.DISABLED)
button_remove.pack(side=tk.LEFT, padx=2)

label_head1 = ttk.Label(frame_list1, text='0 items')
label_head1.grid(row=1, column=0, columnspan=2)

s_list1 = tk.StringVar()
listbox_list1 = tk.Listbox(frame_list1, listvariable=s_list1, activestyle='none', selectmode='extended')
listbox_list1.grid(row=2, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list1 = ttk.Scrollbar(frame_list1, orient=tk.VERTICAL, command=listbox_list1.yview)
scrollbar_list1.grid(row=2, column=1, sticky=(tk.N, tk.S))
listbox_list1["yscrollcommand"] = scrollbar_list1.set

xscrollbar_list1 = ttk.Scrollbar(frame_list1, orient=tk.HORIZONTAL, command=listbox_list1.xview)
xscrollbar_list1.grid(row=3, column=0, sticky=(tk.E, tk.W))
listbox_list1["xscrollcommand"] = xscrollbar_list1.set

# List of finished items
frame_list2 = ttk.Frame(frame_middle, padding=(6,2,6,6), relief='groove')
frame_list2.grid(row=0, column=1, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list2.columnconfigure(0, weight=1)
frame_list2.rowconfigure(2, weight=1)

frame_top2 = ttk.Frame(frame_list2, padding=(0,4,0,3))
frame_top2.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

button_open2 = ttk.Button(frame_top2, text="Open with MultiPar", width=20, command=button_open2_clicked, state=tk.DISABLED)
button_open2.pack(side=tk.LEFT, padx=2)

label_head2 = ttk.Label(frame_list2, text='0 finished items')
label_head2.grid(row=1, column=0, columnspan=2)

s_list2 = tk.StringVar()
listbox_list2 = tk.Listbox(frame_list2, listvariable=s_list2, activestyle='none')
listbox_list2.grid(row=2, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list2 = ttk.Scrollbar(frame_list2, orient=tk.VERTICAL, command=listbox_list2.yview)
scrollbar_list2.grid(row=2, column=1, sticky=(tk.N, tk.S))
listbox_list2["yscrollcommand"] = scrollbar_list2.set

xscrollbar_list2 = ttk.Scrollbar(frame_list2, orient=tk.HORIZONTAL, command=listbox_list2.xview)
xscrollbar_list2.grid(row=3, column=0, sticky=(tk.E, tk.W))
listbox_list2["xscrollcommand"] = xscrollbar_list2.set

# Status text
frame_bottom = ttk.Frame(root)
frame_bottom.grid(row=2, column=0, sticky=(tk.E,tk.W))

label_status = ttk.Label(frame_bottom, text='Select folders and/or files to create PAR files.')
label_status.pack(side=tk.LEFT, padx=2)


# When a folder is specified in command-line
if len(sys.argv) == 2:
    one_path = os.path.abspath(sys.argv[1])
    if os.path.isdir(one_path):
        search_child_item(one_path)
    else:
        add_argv_item()

# When multiple items are specified
elif len(sys.argv) > 2:
    add_argv_item()

# If you want to start creation automatically, use below lines.
#if listbox_list1.size() > 0:
#    button_parent.config(state=tk.DISABLED)
#    button_child.config(state=tk.DISABLED)
#    button_file.config(state=tk.DISABLED)
#    button_stop.config(state=tk.NORMAL)
#    root.after(100, queue_run)


# Show window
root.mainloop()
