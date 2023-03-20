import os
import re
import sys
import glob
import struct
import hashlib
import tkinter as tk
from tkinter import ttk
from tkinter import filedialog


# Initialize global variables
folder_path = "./"
set_id = None
old_name = []


# Search PAR files of same base name
def search_par_file(file_path):
    global folder_path

    # Check file type by filename extension
    file_name = os.path.basename(file_path)
    file_base, file_ext = os.path.splitext(file_name)
    if file_ext.lower() != '.par2':
        label_status.config(text= "Selected one isn't PAR2 file.")
        return

    # Save directory of selected file
    #print("path=", file_path)
    folder_path = os.path.dirname(file_path)
    #print("path=", folder_path)
    if folder_path == "":
        folder_path = '.'

    # Clear list of PAR files
    old_name.clear()
    listbox_list1.delete(0, tk.END)
    listbox_list2.delete(0, tk.END)
    button_name.config(state=tk.DISABLED)
    button_save.config(state=tk.DISABLED)

    # Compare filename in case insensitive
    base_name = file_base.lower()
    # Remove ".vol#-#", ".vol#+#", or ".vol_#" at the last
    base_name = re.sub(r'[.]vol\d*[-+_]\d+$', "", base_name)
    #print("base=", base_name)
    listbox_list1.insert(tk.END, file_name)
    file_count = 1

    # Search other PAR2 files of same base name
    for another_path in glob.glob(glob.escape(folder_path + "/" + base_name) + "*.par2"):
        #print("path=", another_path)
        another_name = os.path.basename(another_path)
        if another_name == file_name:
            continue
        listbox_list1.insert(tk.END, another_name)
        file_count += 1

    # Ready to read PAR2 files
    button_read.config(state=tk.NORMAL)
    label_status.config(text= "There are " + str(file_count) + " PAR2 files of same base name.")


# Select file to search other PAR files
def button_file_clicked():
    global folder_path

    # Show file selecting dialog
    file_type = [("PAR2 file", "*.par2"), ('All files', '*')]
    file_path = filedialog.askopenfilename(filetypes=file_type, initialdir=folder_path, title="Select a PAR2 file to search others")
    if file_path == "":
        return
    search_par_file(file_path)


# Read filenames in a PAR file
def button_read_clicked():
    global folder_path, set_id

    # Clear list of source files
    set_id = None
    old_name.clear()
    listbox_list2.delete(0, tk.END)

    # Get name of a selected file.
    # If not selected, get the first file.
    indices = listbox_list1.curselection()
    if len(indices) == 1:
        file_name = listbox_list1.get(indices[0])
    else:
        file_name = listbox_list1.get(0)
    label_status.config(text= "Reading " + file_name + " ...")


    # Open the selected PAR file and read the first 2 MB.
    # Main packet and File Description packet would be smaller than 1 MB.
    f = open(folder_path + "/" + file_name, 'rb')
    data = f.read(2097152)
    data_size = len(data)

    # Initialize
    file_count = 0
    count_now = 0

    # Search PAR2 packets
    offset = 0
    while offset + 64 < data_size:
        if data[offset : offset + 8] == b'PAR2\x00PKT':
            # Packet size
            packet_size = struct.unpack_from('Q', data, offset + 8)[0]
            #print("size=", packet_size)

            # If a packet is larger than buffer size, just ignore it.
            if offset + packet_size > data_size:
                offset += 8
                continue

            # Checksum of the packet
            hash = hashlib.md5(data[offset + 32 : offset + packet_size]).digest()
            #print(hash)
            if data[offset + 16 : offset + 32] == hash:
                #print("MD5 is same.")

                # Set ID
                if set_id == None:
                    set_id = data[offset + 32 : offset + 48]
                    #print("Set ID=", set_id)
                elif set_id != data[offset + 32 : offset + 48]:
                    #print("Set ID is different.")
                    offset += packet_size
                    continue

                # Packet type
                if data[offset + 48 : offset + 64] == b'PAR 2.0\x00Main\x00\x00\x00\x00':
                    #print("Main packet")
                    file_count = struct.unpack_from('I', data, offset + 72)[0]
                    #print("Number of source files=", file_count)
                    if file_count == 0:
                        break
                    elif count_now == file_count:
                        break

                elif data[offset + 48 : offset + 64] == b'PAR 2.0\x00FileDesc':
                    #print("File Description packet")
                    # Remove padding null bytes
                    name_end = packet_size
                    while data[offset + name_end - 1] == 0:
                        name_end -= 1
                    #print("Filename length=", name_end - 64 - 56)
                    file_name = data[offset + 120 : offset + name_end].decode("UTF-8")
                    #print("Filename=", file_name)
                    # Ignore same name, if the name exists in the list already.
                    if file_name in old_name:
                        offset += packet_size
                        continue
                    old_name.append(file_name)
                    listbox_list2.insert(tk.END, file_name)
                    count_now += 1
                    if count_now == file_count:
                        break

                offset += packet_size
            else:
                offset += 8
        else:
            offset += 1

        # When it reaches to half, read next 1 MB from the PAR file.
        if offset >= 1048576:
            #print("data_size=", data_size, "offset=", offset)
            data = data[offset : data_size] + f.read(1048576)
            data_size = len(data)
            offset = 0

    # Close file
    f.close()

    if file_count > 0:
        button_name.config(state=tk.NORMAL)
    else:
        button_name.config(state=tk.DISABLED)
    button_save.config(state=tk.DISABLED)
    label_status.config(text= "The PAR2 file includes " + str(file_count) + " source files.")


# Edit a name of a source file
def button_name_clicked():
    # Get current name of renaming file.
    # If not selected, show error message.
    indices = listbox_list2.curselection()
    if len(indices) == 1:
        current_name = listbox_list2.get(indices[0])
    else:
        label_status.config(text= "Select a source file to rename.")
        return

    # Get new name and check invalid characters
    new_name = entry_edit.get()
    if new_name == "":
        label_status.config(text= "Enter new name in text box.")
        return
    elif new_name == current_name:
        label_status.config(text= "New name is same as old one.")
        return
    elif "'" + new_name + "'" in s_list2.get():
        label_status.config(text= "New name is same as another filename.")
        return
    elif '\\' in new_name:
        label_status.config(text= "Filename cannot include \\.")
        return
    elif ':' in new_name:
        label_status.config(text= "Filename cannot include :.")
        return
    elif '*' in new_name:
        label_status.config(text= "Filename cannot include *.")
        return
    elif '?' in new_name:
        label_status.config(text= "Filename cannot include ?.")
        return
    elif '\"' in new_name:
        label_status.config(text= "Filename cannot include \".")
        return
    elif '<' in new_name:
        label_status.config(text= "Filename cannot include <.")
        return
    elif '>' in new_name:
        label_status.config(text= "Filename cannot include >.")
        return
    elif '|' in new_name:
        label_status.config(text= "Filename cannot include |.")
        return

    # Add new name and remove old one
    index = indices[0]
    listbox_list2.insert(index + 1, new_name)
    listbox_list2.delete(index)
    label_status.config(text= "Renamed from \"" + current_name + "\" to \"" + new_name + "\"")

    # Only when names are changed, it's possible to save.
    index = 0
    for name_one in old_name:
        if listbox_list2.get(index) != name_one:
            index = -1
            break
        index += 1
    if index < 0:
        button_save.config(state=tk.NORMAL)
    else:
        button_save.config(state=tk.DISABLED)


# Save as new PAR files
def button_save_clicked():
    global folder_path, set_id

    # Allocate 64 KB buffer for File Description packet
    buffer = bytearray(65536)
    rename_count = 0

    # For each PAR file
    file_index = 0
    file_count = listbox_list1.size()
    while file_index < file_count:
        file_name = listbox_list1.get(file_index)
        file_index += 1
        #print("Target=", file_name)

        # Open a PAR file
        fr = open(folder_path + "/" + file_name, 'rb')
        data = fr.read(2097152)
        data_size = len(data)

        # Name of new PAR files have prefix "new_".
        fw = open(folder_path + "/new_" + file_name, 'wb')

        # Search PAR2 packets
        offset = 0
        while offset + 64 < data_size:
            if data[offset : offset + 8] == b'PAR2\x00PKT':
                # Packet size
                packet_size = struct.unpack_from('Q', data, offset + 8)[0]
                #print("size=", packet_size)

                # If a packet is larger than buffer size, just ignore it.
                if offset + packet_size > data_size:
                    fw.write(data[offset : offset + 8])
                    offset += 8
                    continue

                # Checksum of the packet
                hash = hashlib.md5(data[offset + 32 : offset + packet_size]).digest()
                #print(hash)
                if data[offset + 16 : offset + 32] == hash:
                    #print("MD5 is same.")

                    # Set ID
                    if set_id != data[offset + 32 : offset + 48]:
                        #print("Set ID is different.")
                        fw.write(data[offset : offset + packet_size])
                        offset += packet_size
                        continue

                    # Packet type
                    index = -1
                    if data[offset + 48 : offset + 64] == b'PAR 2.0\x00FileDesc':
                        #print("File Description packet")
                        # Remove padding null bytes
                        name_end = packet_size
                        while data[offset + name_end - 1] == 0:
                            name_end -= 1
                        #print("Filename length=", name_end - 64 - 56)
                        name_one = data[offset + 120 : offset + name_end].decode("UTF-8")
                        #print("Filename=", name_one)
                        if name_one in old_name:
                            index = old_name.index(name_one)
                            #print("index=", index)
                            new_name = listbox_list2.get(index)
                            if new_name != name_one:
                                # Copy from old packet
                                buffer[0 : 120] = data[offset : offset + 120]

                                # Set new name
                                #print("New name=", new_name, "old name=", name_one)
                                name_byte = new_name.encode("UTF-8")
                                name_len = len(name_byte)
                                #print("byte=", name_byte, "len=", name_len)
                                buffer[120 : 120 + name_len] = name_byte

                                # Padding null bytes
                                while name_len % 4 != 0:
                                    buffer[120 + name_len] = 0
                                    name_len += 1
                                #print("padded len=", name_len)

                                # Update packet size
                                size_byte = struct.pack('Q', 120 + name_len)
                                buffer[8 : 16] = size_byte
                                #print("packet=", buffer[0 : 120 + name_len])

                                # Update checksum of packet
                                hash = hashlib.md5(buffer[32 : 120 + name_len]).digest()
                                buffer[16 : 32] = hash

                                # Write new packet
                                fw.write(buffer[0 : 120 + name_len])
                                rename_count += 1

                            # When filename isn't changed.
                            else:
                                index = -2

                    # Write packet with current data
                    if index < 0:
                        fw.write(data[offset : offset + packet_size])

                    offset += packet_size
                else:
                    fw.write(data[offset : offset + 8])
                    offset += 8
            else:
                fw.write(data[offset : offset + 1])
                offset += 1

            # When it reaches to half, read next.
            if offset >= 1048576:
                #print("data_size=", data_size, "offset=", offset)
                data = data[offset : data_size] + fr.read(1048576)
                data_size = len(data)
                offset = 0

        # Close file
        fr.close()
        fw.close()

    label_status.config(text= "Modified " + str(rename_count) + " packets in " + str(file_count) + " PAR2 files.")


# Window size and title
root = tk.Tk()
root.title('PAR2 Rename')
root.minsize(width=480, height=240)
# Centering window
init_width = 640
init_height = 480
init_left = (root.winfo_screenwidth() - init_width) // 2
init_top = (root.winfo_screenheight() - init_height) // 2
root.geometry('{}x{}+{}+{}'.format(init_width, init_height, init_left, init_top))
#root.geometry("640x480")
root.columnconfigure(0, weight=1)
root.rowconfigure(0, weight=1)

# Body
frame_body = ttk.Frame(root, padding=(2,6,2,2))
frame_body.grid(row=0, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_body.rowconfigure(0, weight=1)
frame_body.columnconfigure(0, weight=1)
frame_body.columnconfigure(1, weight=1)

# List of PAR files
frame_list1 = ttk.Frame(frame_body, padding=(6,2,6,6), relief='groove')
frame_list1.grid(row=0, column=0, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list1.columnconfigure(0, weight=1)
frame_list1.rowconfigure(1, weight=1)

frame_top1 = ttk.Frame(frame_list1, padding=(0,4,0,3))
frame_top1.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

button_file = ttk.Button(frame_top1, text="File", width=9, command=button_file_clicked)
button_file.pack(side=tk.LEFT, padx=2)

button_read = ttk.Button(frame_top1, text="Read", width=9, command=button_read_clicked, state=tk.DISABLED)
button_read.pack(side=tk.LEFT, padx=2)

button_save = ttk.Button(frame_top1, text="Save", width=9, command=button_save_clicked, state=tk.DISABLED)
button_save.pack(side=tk.LEFT, padx=2)

s_list1 = tk.StringVar()
listbox_list1 = tk.Listbox(frame_list1, listvariable=s_list1, activestyle=tk.NONE)
listbox_list1.grid(row=1, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list1 = ttk.Scrollbar(frame_list1, orient=tk.VERTICAL, command=listbox_list1.yview)
scrollbar_list1.grid(row=1, column=1, sticky=(tk.N, tk.S))
listbox_list1["yscrollcommand"] = scrollbar_list1.set

# List of source files
frame_list2 = ttk.Frame(frame_body, padding=(6,2,6,6), relief='groove')
frame_list2.grid(row=0, column=1, padx=4, sticky=(tk.E,tk.W,tk.S,tk.N))
frame_list2.columnconfigure(0, weight=1)
frame_list2.rowconfigure(1, weight=1)

frame_top2 = ttk.Frame(frame_list2, padding=(0,4,0,3))
frame_top2.grid(row=0, column=0, columnspan=2, sticky=(tk.E,tk.W))

s_edit2 = tk.StringVar()
entry_edit = ttk.Entry(frame_top2,textvariable=s_edit2)
entry_edit.grid(row=0, column=0, padx=2, sticky=(tk.E,tk.W))
frame_top2.columnconfigure(0, weight=1)

button_name = ttk.Button(frame_top2, text="Rename", width=9, command=button_name_clicked, state=tk.DISABLED)
button_name.grid(row=0, column=1, padx=2)

s_list2 = tk.StringVar()
listbox_list2 = tk.Listbox(frame_list2, listvariable=s_list2, activestyle=tk.NONE)
listbox_list2.grid(row=1, column=0, sticky=(tk.E,tk.W,tk.S,tk.N))

scrollbar_list2 = ttk.Scrollbar(frame_list2, orient=tk.VERTICAL, command=listbox_list2.yview)
scrollbar_list2.grid(row=1, column=1, sticky=(tk.N, tk.S))
listbox_list2["yscrollcommand"] = scrollbar_list2.set

# Status text
frame_foot = ttk.Frame(root)
frame_foot.grid(row=1, column=0, sticky=(tk.E,tk.W))

label_status = ttk.Label(frame_foot, text='Select a PAR2 file to rename included files.', width=100)
label_status.pack(side=tk.LEFT, padx=2)


# When file is specified in command-line
if len(sys.argv) > 1:
    file_path = sys.argv[1]
    if os.path.isfile(file_path):
        #file_path = os.path.abspath(file_path)
        search_par_file(file_path)


# Show window
root.mainloop()
