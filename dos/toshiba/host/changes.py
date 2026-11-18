"""Rename and delete from the C64 (protocol v1, /c64/v1/files/rename and
/c64/v1/files/delete).

Together with uploads.py this is the only code that changes files. Rules:
- the entry and its folder are inside the storage root; symlinks are never
  renamed or deleted;
- rename stays in the same folder and never replaces an existing entry;
- delete never erases anything: the entry is moved into the hidden folder
  TRASH_NAME at the top of the storage root, under a name that says where it
  came from and when. Folders must be empty. Emptying the trash is up to the
  user, on the PC (the user's decision, 2026-09-25).
"""
import os
import struct
import threading
import time

import files

TRASH_NAME = ".c64trash"
STATUS_NOT_EMPTY = 13       # delete: the folder is not empty
STATUS_EXISTS = 8           # rename: the new name exists (same code as uploads)

CHANGE_REPLY = struct.Struct("<BB")     # version, status

_lock = threading.Lock()


def reply(status):
    return CHANGE_REPLY.pack(files.PROTOCOL_VERSION, status)


def entry_in_folder(root, folder_path, name):
    """The entry `name` in folder `folder_path`, checked like uploads."""
    if folder_path and not folder_path.endswith("/"):
        raise files.RequestError(files.STATUS_BAD_REQUEST)
    if len(folder_path) > files.PATH_MAX or not files.is_c64_name(name):
        raise files.RequestError(files.STATUS_BAD_REQUEST)
    folder = files.resolve(root, folder_path)
    if not folder.is_dir():
        raise files.RequestError(files.STATUS_WRONG_TYPE)
    entry = folder / name
    if entry.is_symlink():
        raise files.RequestError(files.STATUS_ACCESS_DENIED)
    if not entry.exists():
        raise files.RequestError(files.STATUS_NOT_FOUND)
    if not (entry.is_file() or entry.is_dir()):
        raise files.RequestError(files.STATUS_ACCESS_DENIED)
    return folder, entry


def rename(root, folder_path, name, new_name, log=None):
    with _lock:
        try:
            folder, entry = entry_in_folder(root, folder_path, name)
            if not files.is_c64_name(new_name):
                raise files.RequestError(files.STATUS_BAD_REQUEST)
            if new_name == name:
                return reply(files.STATUS_OK)
            target = folder / new_name
            # A different entry of that name (on a case-insensitive file
            # system "readme.txt" and "README.TXT" are the same entry).
            if (target.exists() or target.is_symlink()) and not \
                    (target.exists() and os.path.samefile(target, entry)):
                raise files.RequestError(STATUS_EXISTS)
            os.rename(entry, target)
        except files.RequestError as e:
            return reply(e.status)
        except OSError:
            return reply(files.STATUS_ACCESS_DENIED)
    if log:
        log.info("RENAME %s%s -> %s", folder_path, name, new_name)
    return reply(files.STATUS_OK)


def trash_name(folder_path, name):
    """'20260925-101500 docs__letters__OLD.TXT': when, and where from."""
    origin = (folder_path + name).replace("/", "__")
    return time.strftime("%Y%m%d-%H%M%S ") + origin


def delete(root, folder_path, name, log=None):
    with _lock:
        try:
            folder, entry = entry_in_folder(root, folder_path, name)
            if entry.is_dir() and any(entry.iterdir()):
                raise files.RequestError(STATUS_NOT_EMPTY)
            trash = files.resolve(root, "") / TRASH_NAME
            trash.mkdir(exist_ok=True)
            if trash.is_symlink() or not trash.is_dir():
                raise files.RequestError(files.STATUS_ACCESS_DENIED)
            base = trash_name(folder_path, name)
            target, counter = trash / base, 1
            while target.exists() or target.is_symlink():
                counter += 1
                target = trash / ("%s (%d)" % (base, counter))
            os.rename(entry, target)
        except files.RequestError as e:
            return reply(e.status)
        except OSError:
            return reply(files.STATUS_ACCESS_DENIED)
    if log:
        log.info("DELETE %s%s -> %s/%s", folder_path, name, TRASH_NAME, target.name)
    return reply(files.STATUS_OK)
