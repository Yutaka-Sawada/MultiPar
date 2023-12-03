import sys
import os
import glob
import re
import subprocess
import tkinter as tk
from tkinter import ttk
from tkinter import filedialog


# Set path of MultiPar
client_path = "../par2j64.exe"
gui_path = "../MultiPar.exe"
save_path = "../save"


# Initialize global variables
folder_path = ""
sub_proc = None


# Search PAR2 sets in a folder
def search_par_set(one_path):
    if one_path == "":
        return None

    # Clear list-box at first
    listbox_list1.delete(0, tk.END)
    listbox_list2.delete(0, tk.END)
    listbox_list3.delete(0, tk.END)
    button_start.config(state=tk.DISABLED)
    
    pendings = set(s_list1.get())
    one_path_len = len(one_path)

    # Search all folders and sub-folders recursively
    for path_root, path_dirs, path_files in os.walk(one_path, topdown=True, onerror=None, followlinks=False):
        seen = set()
        for base_name in path_files:
            if not base_name.endswith(".par2"):
                continue
            # Dump the file extension '.par2' from the filename            
            base_name = base_name[:-5]
            # Compare filename in case insensitive
            base_name.lower()
            # Remove ".vol#-#", ".vol#+#", or ".vol_#" at the last
            base_name = re.sub(r'[.]vol\d*[-+_]\d+$', "", base_name)
            # Ignore same base, if the name exists in the list already.
            if base_name in seen:
                continue
            else:
                seen.add(base_name)
            par_path = os.path.join(path_root[one_path_len:], base_name) 
            if par_path in pendings:
                continue
            else:
                pendings.add(par_path)
            # Add found unique PAR sets
            listbox_list1.insert(tk.END, par_path)
            
    item_count = listbox_list1.size()
    #one_name = os.path.basename(one_path)
    #label_status.config(text= "Selected folder: " + one_path)
    if len(pendings) == 0:
        label_status.config(text= "There is no PAR sets.")
        button_stop.config(state=tk.NORMAL)
        button_open2.config(state=tk.DISABLED)
        button_open3.config(state=tk.DISABLED)
    else:
        label_head1.config(text= str(item_count) + " sets in " + one_path + ". Click Start button to verfy them.")
        button_start.config(state=tk.NORMAL)
        # Do not start automatically
        #button_folder.config(state=tk.DISABLED)
        #button_stop.config(state=tk.NORMAL)
        #button_open2.config(state=tk.DISABLED)
        #button_open3.config(state=tk.DISABLED)
        ## Start verification automatically
        #root.after(100, queue_run)


# Select folder to search PAR files
def button_folder_clicked():
    global folder_path
    if folder_path == "":
        s_initialdir = "./"
    else:
        s_initialdir = os.path.dirname(folder_path)
        
    #a = filedialog.askopenfilename(initialdir="/", title="Select file",
    #    filetypes=(("txt files", "*.txt"),("all files", "*.*")))    
    
    import inspect
    
    folder_path = filedialog.askdirectory(
        initialdir=s_initialdir, 
        title="Select the folder containing Par2 files.", 
        )
    if folder_path == "":
        return
    search_par_set(folder_path)


# Verify the first PAR set
def queue_run():
    global folder_path, sub_proc

    if sub_proc != None:
        return

    if "disabled" in button_stop.state():
        button_folder.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_open2.config(state=tk.NORMAL)
        button_open3.config(state=tk.NORMAL)
        label_status.config(text= "Stopped queue")
        return

    if folder_path == "":
        label_status.config(text= "Select folder at first.")
        return
    base_name = listbox_list1.get(0)
    if base_name == "":
        label_status.config(text= "There is no PAR sets.")
        return

    one_path = folder_path + "\\" + base_name + ".par2"
    label_status.config(text= "Verifying " + base_name)

    # Set command-line
    # Cover path by " for possible space   
    cmd = "\"" + client_path + "\" v /fo /vs2 /vd\"" + save_path + "\" \"" + one_path + "\""
    # If you want to repair a damaged set automatically, use "r" command instead of "v".
    print(cmd)

    # Run PAR2 client
    sub_proc = subprocess.Popen(cmd, shell=True)

    # Wait finish of verification
    root.after(500, queue_result)


# Wait and read verification result
def queue_result():
    global folder_path, sub_proc

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
    base_name = listbox_list1.get(0)

    # When all source files are complete
    if (exit_code == 0) or (exit_code == 256):
        # Add to list of complete set
        listbox_list3.insert(tk.END, base_name)
        item_count = listbox_list3.size()
        label_head3.config(text= str(item_count) + " complete sets")

    # When fatal error happened in par2j
    elif exit_code == 1:
        button_folder.config(state=tk.NORMAL)
        button_stop.config(state=tk.DISABLED)
        button_open2.config(state=tk.NORMAL)
        button_open3.config(state=tk.NORMAL)
        label_status.config(text= "Failed queue")
        return

    # When you cancel par2j on Command Prompt
    elif exit_code == 2:
        button_folder.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_stop.config(state=tk.DISABLED)
        button_open2.config(state=tk.NORMAL)
        button_open3.config(state=tk.NORMAL)
        label_status.config(text= "Canceled queue")
        return

    # When source files are bad
    else:
        #print("exit code =", exit_code)
        # Add to list of bad set
        listbox_list2.insert(tk.END, base_name)
        item_count = listbox_list2.size()
        label_head2.config(text= str(item_count) + " bad sets")

    # Remove the first item from the list
    listbox_list1.delete(0)

    # Process next set
    item_count = listbox_list1.size()
    if item_count == 0:
        button_folder.config(state=tk.NORMAL)
        button_stop.config(state=tk.DISABLED)
        button_open2.config(state=tk.NORMAL)
        button_open3.config(state=tk.NORMAL)
        label_status.config(text= "Verified all PAR sets")

    elif "disabled" in button_stop.state():
        button_folder.config(state=tk.NORMAL)
        button_start.config(state=tk.NORMAL)
        button_open2.config(state=tk.NORMAL)
        button_open3.config(state=tk.NORMAL)
        label_status.config(text= "Interrupted queue")
 
    else:
        root.after(100, queue_run)


# Resume stopped queue
def button_start_clicked():
    global sub_proc

    button_folder.config(state=tk.DISABLED)
    button_start.config(state=tk.DISABLED)
    button_stop.config(state=tk.NORMAL)
    button_open2.config(state=tk.DISABLED)
    button_open3.config(state=tk.DISABLED)

    if sub_proc == None:
        queue_run()
    else:
        queue_result()


# Stop running queue
def button_stop_clicked():
    button_stop.config(state=tk.DISABLED)
    if sub_proc != None:
        label_status.config(text= "Waiting finish of current task")


# Open a PAR2 set by MultiPar
def button_open2_clicked():
    indices = listbox_list2.curselection()
    if len(indices) == 1:
        base_name = listbox_list2.get(indices[0])
        one_path = folder_path + "\\" + base_name + ".par2"
        # Set command-line
        # Cover path by " for possible space   
        cmd = "\"" + gui_path + "\" /verify \"" + one_path + "\""

        # Open MultiPar GUI to see details
        # Because this doesn't wait finish of MultiPar, you may open some at once.
        subprocess.Popen(cmd)


def button_open3_clicked():
    indices = listbox_list3.curselection()
    if len(indices) == 1:
        base_name = listbox_list3.get(indices[0])
        one_path = folder_path + "\\" + base_name + ".par2"
        # Set command-line
        # Cover path by " for possible space   
        cmd = "\"" + gui_path + "\" /verify \"" + one_path + "\""

        # Open MultiPar GUI to see details
        # Because this doesn't wait finish of MultiPar, you may open some at once.
        subprocess.Popen(cmd)


# Window size and title
root = tk.Tk()
root.title('PAR Queue')
root.minsize(width=520, height=200)
root.geometry("800x640")
root.columnconfigure(0, weight=1)
root.rowconfigure(0, weight=1)

# List
frame_middle = ttk.Frame(root, padding=(2,6,2,2))
frame_middle.grid(row=0, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_middle.rowconfigure(0, weight=1)
frame_middle.columnconfigure(0, weight=1)
frame_middle.columnconfigure(1, weight=1)
frame_middle.columnconfigure(2, weight=1)

# List of PAR files
frame_list1 = ttk.Frame(frame_middle, padding=(6,2,6,6), relief='groove')
frame_list1.grid(row=0, column=0, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list1.columnconfigure(0, weight=1)
frame_list1.rowconfigure(2, weight=1)

frame_top1 = ttk.Frame(frame_list1, padding=(0,4,0,3))
frame_top1.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

button_folder = ttk.Button(frame_top1, text="Folder", width=6, command=button_folder_clicked)
button_folder.pack(side=tk.LEFT, padx=2)

button_start = ttk.Button(frame_top1, text="Start", width=6, command=button_start_clicked, state=tk.DISABLED)
button_start.pack(side=tk.LEFT, padx=2)

button_stop = ttk.Button(frame_top1, text="Stop", width=6, command=button_stop_clicked, state=tk.DISABLED)
button_stop.pack(side=tk.LEFT, padx=2)

label_head1 = ttk.Label(frame_list1, text='? sets in a folder')
label_head1.grid(row=1, column=0, columnspan=2)

s_list1 = tk.StringVar()
listbox_list1 = tk.Listbox(frame_list1, listvariable=s_list1, activestyle='none')
listbox_list1.grid(row=2, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list1 = ttk.Scrollbar(frame_list1, orient=tk.VERTICAL, command=listbox_list1.yview)
scrollbar_list1.grid(row=2, column=1, sticky=(tk.N, tk.S))
listbox_list1["yscrollcommand"] = scrollbar_list1.set

# List of bad files
frame_list2 = ttk.Frame(frame_middle, padding=(6,2,6,6), relief='groove')
frame_list2.grid(row=0, column=1, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list2.columnconfigure(0, weight=1)
frame_list2.rowconfigure(2, weight=1)

frame_top2 = ttk.Frame(frame_list2, padding=(0,4,0,3))
frame_top2.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

button_open2 = ttk.Button(frame_top2, text="Open with MultiPar", command=button_open2_clicked, state=tk.DISABLED)
button_open2.pack(side=tk.LEFT, padx=2)

label_head2 = ttk.Label(frame_list2, text='? bad sets')
label_head2.grid(row=1, column=0, columnspan=2)

s_list2 = tk.StringVar()
listbox_list2 = tk.Listbox(frame_list2, listvariable=s_list2, activestyle='none')
listbox_list2.grid(row=2, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list2 = ttk.Scrollbar(frame_list2, orient=tk.VERTICAL, command=listbox_list2.yview)
scrollbar_list2.grid(row=2, column=1, sticky=(tk.N, tk.S))
listbox_list2["yscrollcommand"] = scrollbar_list2.set

# List of complete files
frame_list3 = ttk.Frame(frame_middle, padding=(6,2,6,6), relief='groove')
frame_list3.grid(row=0, column=2, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list3.columnconfigure(0, weight=1)
frame_list3.rowconfigure(2, weight=1)

frame_top3 = ttk.Frame(frame_list3, padding=(0,4,0,3))
frame_top3.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

button_open3 = ttk.Button(frame_top3, text="Open with MultiPar", command=button_open3_clicked, state=tk.DISABLED)
button_open3.pack(side=tk.LEFT, padx=2)

label_head3 = ttk.Label(frame_list3, text='? complete sets')
label_head3.grid(row=1, column=0, columnspan=2)

s_list3 = tk.StringVar()
listbox_list3 = tk.Listbox(frame_list3, listvariable=s_list3, activestyle='none')
listbox_list3.grid(row=2, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list3 = ttk.Scrollbar(frame_list3, orient=tk.VERTICAL, command=listbox_list3.yview)
scrollbar_list3.grid(row=2, column=1, sticky=(tk.N, tk.S))
listbox_list3["yscrollcommand"] = scrollbar_list3.set

# Status text
frame_bottom = ttk.Frame(root)
frame_bottom.grid(row=1, column=0, sticky=(tk.E,tk.W))

label_status = ttk.Label(frame_bottom, text='Select folder to search PAR files.')
label_status.pack(side=tk.LEFT, padx=2)


# When folder is specified in command-line
if len(sys.argv) > 1:
    folder_path = sys.argv[1]
    if os.path.isdir(folder_path):
        folder_name = os.path.basename(folder_path)
        label_head1.config(text="? sets in " + folder_name)
    else:
        folder_path = ""
    search_par_set(folder_path)


# Show window
root.mainloop()
