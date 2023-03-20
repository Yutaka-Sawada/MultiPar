import sys
import os
import glob
import re
import subprocess
import tkinter as tk
from tkinter import ttk
from tkinter import filedialog


# Set path of par2j
client_path = "../par2j64.exe"
gui_path = "../MultiPar.exe"


# Initialize global variables
list_dir = []
dir_index = 0;
current_dir = "./"
list_par = []
list_src = []
list_size = []
list_now = []


# Check a folder exists in list already
def check_dir_exist(one_path):
    global list_dir
    for list_item in list_dir:
        if os.path.samefile(one_path, list_item):
           return True
    return False


# Search PAR2 sets in a folder
def search_par_set(one_path):
    global list_par, list_src, list_size, list_now
    if one_path == "":
        return

    # Clear lists at first
    list_par.clear()
    list_src.clear()
    list_size.clear()
    list_now.clear()
    for item in treeview_list.get_children():
        treeview_list.delete(item)
    found_base = ""
    button_check.config(state=tk.DISABLED)
    button_open.config(state=tk.DISABLED)
    treeview_list.heading('PAR', text="Items in PAR", anchor='center')
    treeview_list.heading('Now', text="Directory content", anchor='center')

    # Add found PAR sets
    for par_path in glob.glob(glob.escape(one_path) + "/*.par2"):
        file_name = os.path.basename(par_path)
        # Remove extension ".par2"
        one_name = os.path.splitext(file_name)[0]
        # Compare filename in case insensitive
        base_name = one_name.lower()
        # Remove ".vol#-#", ".vol#+#", or ".vol_#" at the last
        base_name = re.sub(r'[.]vol\d*[-+_]\d+$', "", base_name)

        # Confirm only one PAR2 set in the directory.
        if found_base != "":
            if base_name != found_base:
                list_par.clear()
                label_status.config(text= "There are multiple PAR sets in \"" + one_path + "\".")
                return
        else:
            found_base = base_name
        #print(file_name)
        list_par.append(file_name)

    if len(list_par) == 0:
        label_status.config(text= "PAR set isn't found in \"" + one_path + "\".")
        return
    label_status.config(text= "Directory = \"" + one_path + "\", PAR file = \"" + list_par[0] + "\"")
    button_check.config(state=tk.NORMAL)
    button_open.config(state=tk.NORMAL)
    # Check automatically
    root.after(100, button_check_clicked)


# Select a folder to search PAR files
def button_folder_clicked():
    global current_dir, list_dir, dir_index
    if os.path.exists(current_dir) == False:
        current_dir = "./"
    search_dir = filedialog.askdirectory(initialdir=current_dir)
    if search_dir == "":
        return
    # This replacing seems to be worthless.
    #search_dir = search_dir.replace('\\', '/')

    # Insert the folder to list
    dir_count = len(list_dir)
    if dir_count == 0:
        dir_index = 0
        list_dir.append(search_dir)
        dir_count += 1
    else:
        # Check the folder exists already
        if check_dir_exist(search_dir):
            dir_index = list_dir.index(search_dir)
        else:
            dir_index += 1
            list_dir.insert(dir_index, search_dir)
            dir_count += 1
    if dir_count > 1:
        root.title('PAR Diff ' + str(dir_index + 1) + '/' + str(dir_count))
    current_dir = os.path.dirname(search_dir)
    search_par_set(search_dir)


# Read text and get source file
def parse_text(output_text):
    global list_src, list_size
    multi_lines = output_text.split('\n')

    # Search starting point of list
    offset = multi_lines.index("Input File list\t:")
    offset += 2
    # Get file size and name
    while offset < len(multi_lines):
        single_line = multi_lines[offset]
        if single_line == "":
            break
        # File size
        single_line = single_line.lstrip()
        fisrt_item = single_line.split()[0]
        file_size = int(fisrt_item)
        list_size.append(file_size)
        #print(file_size)
        # Compare filename in case insensitive
        file_name = single_line.split("\"")[1]
        file_name = file_name.lower()
        list_src.append(file_name)
        #print(file_name)
        offset += 1


# Check PAR files and list source files
def button_check_clicked():
    global list_dir, dir_index, list_par, list_src, list_size, list_now
    if len(list_dir) == 0:
        return
    treeview_list.heading('PAR', text="? items in PAR", anchor='center')
    treeview_list.heading('Now', text="Directory content", anchor='center')
    search_dir = list_dir[dir_index]
    if os.path.exists(search_dir) == False:
        label_status.config(text= "\"" + search_dir + "\" doesn't exist.")
        return
    if len(list_par) == 0:
        return
    if os.path.exists(client_path) == False:
        label_status.config(text= "Cannot call \"" + client_path + "\".  Set path correctly.")
        return

    # Clear lists at first
    list_src.clear()
    list_size.clear()
    list_now.clear()
    for item in treeview_list.get_children():
        treeview_list.delete(item)
    list_lost = []
    add_count = 0
    diff_count = 0

    # Read source files in PAR set
    for par_path in list_par:
        # Call par2j's list command
        cmd = "\"" + client_path + "\" l /uo \"" + search_dir + "/" + par_path + "\""
        res = subprocess.run(cmd, shell=True, capture_output=True, encoding='utf8')
        #print("return code: {}".format(res.returncode))
        #print("captured stdout: {}".format(res.stdout))
        if res.returncode == 0:
            #label_status.config(text= "Read \"" + par_path + "\" ok.")
            parse_text(res.stdout)
            break
    if (len(list_src) == 0) or (len(list_src) != len(list_size)):
        label_status.config(text= "Failed to read source files in the PAR set.")
        return

    # Get current directory-tree
    for dirs, subdirs, files in os.walk(search_dir):
        # Get sub-directory from base directory
        sub_dir = dirs.lstrip(search_dir)
        sub_dir = sub_dir.replace('\\', '/')
        sub_dir = sub_dir.lstrip('/')
        sub_dir = sub_dir.lower()
        if sub_dir != "":
            sub_dir += "/"
        # Add folders
        for dir_name in subdirs:
            item_name = sub_dir + dir_name.lower() + "/"
            list_now.append(item_name)
            #print("folder：", item_name)
        # Add files
        for file_name in files:
            item_name = sub_dir + file_name
            if (sub_dir == "") and (item_name in list_par):
                continue
            item_name = item_name.lower()
            list_now.append(item_name)
            #print("file:", item_name)

    # Make list of missing items
    for item_name in list_src:
        #print(item_name)
        if not item_name in list_now:
            # The item doesn't exit now.
            list_lost.append(item_name)
            list_now.append(item_name)

    # Compare lists to find additional items
    list_now.sort()
    for item_name in list_now:
        #print(item_name)
        if item_name.endswith("/"):
            # This item is a folder.
            if item_name in list_lost:
                treeview_list.insert(parent='', index='end', values=(item_name, ""), tags='red')
            elif item_name in list_src:
                if not bool_diff.get():
                    treeview_list.insert(parent='', index='end', values=(item_name, item_name))
            else:
                find_flag = 0
                for src_name in list_src:
                    if src_name.startswith(item_name):
                        # The folder exists as sub-directory.
                        find_flag = 1
                        break;
                if find_flag == 0:
                    # The folder doesn't exit in PAR set.
                    treeview_list.insert(parent='', index='end', values=("", item_name), tags='blue')
                    add_count += 1;
        else:
            # This item is a file.
            if item_name in list_lost:
                treeview_list.insert(parent='', index='end', values=(item_name, ""), tags='red')
            elif item_name in list_src:
                file_path = search_dir + "/" + item_name
                item_index = list_src.index(item_name)
                file_size = os.path.getsize(file_path)
                #print(item_index, list_size[item_index], file_size)
                if file_size == list_size[item_index]:
                    if not bool_diff.get():
                        treeview_list.insert(parent='', index='end', values=(item_name, item_name))
                else:
                    treeview_list.insert(parent='', index='end', values=(item_name, item_name), tags='yellow')
                    diff_count += 1
            else:
                # The file doesn't exit in PAR set.
                treeview_list.insert(parent='', index='end', values=("", item_name), tags='blue')
                add_count += 1;

    # Number of missing or additional items
    item_count = len(list_src)
    lost_count = len(list_lost)
    if lost_count == 0:
        treeview_list.heading('PAR', text= str(item_count) + " items in PAR", anchor='center')
    else:
        treeview_list.heading('PAR', text= str(item_count) + " items in PAR ( " + str(lost_count) + " miss )", anchor='center')
    if add_count + diff_count > 0:
        if add_count == 0:
            treeview_list.heading('Now', text="Directory content ( " + str(diff_count) + " diff )", anchor='center')
        elif diff_count == 0:
            treeview_list.heading('Now', text="Directory content ( " + str(add_count) + " add )", anchor='center')
        else:
            treeview_list.heading('Now', text="Directory content ( " + str(add_count) + " add, " + str(diff_count) + " diff )", anchor='center')

    # If you want to see some summary, uncomment below section.
    #status_text = "Directory = \"" + search_dir + "\", PAR file = \"" + par_path + "\"\nTotal items = " + str(item_count) + ". "
    #if lost_count + add_count + diff_count == 0:
    #    status_text += "Directory and Par File structure match."
    #else:
    #    if lost_count > 0:
    #        status_text += str(lost_count) + " items are missing. "
    #    if add_count > 0:
    #        status_text += str(add_count) + " items are additional. "
    #    if diff_count > 0:
    #        status_text += str(diff_count) + " items are different size."
    #label_status.config(text=status_text)


# Open PAR set by MultiPar
def button_open_clicked():
    global list_dir, dir_index, list_par
    if len(list_dir) == 0:
        return
    search_dir = list_dir[dir_index]
    if os.path.exists(search_dir) == False:
        return
    if len(list_par) == 0:
        return
    if os.path.exists(gui_path) == False:
        label_status.config(text= "Cannot call \"" + gui_path + "\". Set path correctly.")
        return

    # Set command-line
    # Cover path by " for possible space   
    par_path = search_dir + "/" + list_par[0]
    cmd = "\"" + gui_path + "\" /verify \"" + par_path + "\""

    # Open MultiPar GUI to see details
    # Because this doesn't wait finish of MultiPar, you may open some at once.
    subprocess.Popen(cmd)


# Move to next folder
def button_next_clicked():
    global current_dir, list_dir, dir_index
    dir_count = len(list_dir)
    search_dir = ""

    # Goto next directory
    while dir_count > 1:
        dir_index += 1
        if dir_index >= dir_count:
            dir_index = 0
        search_dir = list_dir[dir_index]
        # Check the directory exists
        if os.path.exists(search_dir):
            break
        else:
            list_dir.pop(dir_index)
            dir_index -= 1
            dir_count -= 1
            search_dir = ""

    if search_dir == "":
        return
    root.title('PAR Diff ' + str(dir_index + 1) + '/' + str(dir_count))
    current_dir = os.path.dirname(search_dir)
    search_par_set(search_dir)


# Window size and title
root = tk.Tk()
root.title('PAR Diff')
root.minsize(width=480, height=240)
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

button_next = ttk.Button(frame_top, text="Next folder", width=12, command=button_next_clicked)
button_next.pack(side=tk.LEFT, padx=2)

button_folder = ttk.Button(frame_top, text="Select folder", width=12, command=button_folder_clicked)
button_folder.pack(side=tk.LEFT, padx=2)

button_check = ttk.Button(frame_top, text="Check again", width=12, command=button_check_clicked, state=tk.DISABLED)
button_check.pack(side=tk.LEFT, padx=2)

button_open = ttk.Button(frame_top, text="Open with MultiPar", width=19, command=button_open_clicked, state=tk.DISABLED)
button_open.pack(side=tk.LEFT, padx=2)

bool_diff = tk.BooleanVar()
bool_diff.set(False) # You may change initial state here.
check_diff = ttk.Checkbutton(frame_top, variable=bool_diff, text="Diff only")
check_diff.pack(side=tk.LEFT, padx=2)

# List
frame_middle = ttk.Frame(root, padding=(4,2,4,2))
frame_middle.grid(row=1, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_middle.rowconfigure(0, weight=1)
frame_middle.columnconfigure(0, weight=1)

column = ('PAR', 'Now')
treeview_list = ttk.Treeview(frame_middle, columns=column, selectmode='none')
treeview_list.grid(row=0, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))
treeview_list.column('#0',width=0, stretch='no')
treeview_list.heading('#0', text='')
treeview_list.heading('PAR', text="Items in PAR", anchor='center')
treeview_list.heading('Now', text="Directory content", anchor='center')
treeview_list.tag_configure('red', background='#FFC0C0')
treeview_list.tag_configure('blue', background='#80FFFF')
treeview_list.tag_configure('yellow', background='#FFFF80')

scrollbar_list = ttk.Scrollbar(frame_middle, orient=tk.VERTICAL, command=treeview_list.yview)
scrollbar_list.grid(row=0, column=1, sticky=(tk.N, tk.S))
treeview_list["yscrollcommand"] = scrollbar_list.set

xscrollbar_list = ttk.Scrollbar(frame_middle, orient=tk.HORIZONTAL, command=treeview_list.xview)
xscrollbar_list.grid(row=1, column=0, sticky=(tk.E, tk.W))
treeview_list["xscrollcommand"] = xscrollbar_list.set

# Status text
frame_bottom = ttk.Frame(root)
frame_bottom.grid(row=2, column=0, sticky=(tk.E,tk.W))

label_status = ttk.Label(frame_bottom, text='Select folder to check difference.')
label_status.pack(side=tk.LEFT, padx=2)


# When folders are specified in command-line
if len(sys.argv) > 1:
    search_dir = ""
    for one_argv in sys.argv[1:]:
        if os.path.isdir(one_argv):
            one_path = os.path.abspath(one_argv)
            one_path = one_path.replace('\\', '/')
            if check_dir_exist(one_path) == False:
                #print("argv=", one_path)
                list_dir.append(one_path)
                if search_dir == "":
                    search_dir = one_path
    dir_count = len(list_dir)
    if dir_count > 1:
        root.title('PAR Diff 1/' + str(dir_count))
    if search_dir != "":
        current_dir = os.path.dirname(search_dir)
        search_par_set(search_dir)


# Show window
root.mainloop()
