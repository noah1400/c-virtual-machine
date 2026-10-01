#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cpu.h"
#include "devices.h"
#include "vm.h"

#define DISK_SECTOR_SIZE 512
#define DISK_MAX_SECTORS 0x400000u     // 2 GB

enum { DISK_READ = 1, DISK_WRITE = 2, DISK_RESIZE = 3 };

// Command results
enum { DISK_OK, DISK_NO_DISK, DISK_BAD_SECTOR, DISK_BAD_BUFFER, DISK_BAD_COMMAND, DISK_READ_ONLY, DISK_HOST_ERROR };

typedef struct {
    int fd;                 // -1 while no image is attached
    int read_only;
    uint32_t sectors;
} Drive;

typedef struct {
    Drive drives[DISK_DRIVES];
    uint32_t drive;         // the one that commands and the size port go to
    uint32_t sector, buffer, count, status;
} DiskState;

static Drive *selected(DiskState *disk) {
    return disk->drive < DISK_DRIVES && disk->drives[disk->drive].fd >= 0 ? &disk->drives[disk->drive] : NULL;
}

// Copies count sectors starting at sector between the image and physical memory at buffer
static uint32_t disk_transfer(VM *vm, DiskState *disk, Drive *drive, uint32_t command) {
    if (disk->sector > drive->sectors || disk->count > drive->sectors - disk->sector) {
        return DISK_BAD_SECTOR;
    }
    uint64_t size = (uint64_t)disk->count * DISK_SECTOR_SIZE;
    if (disk->buffer + size > vm->memory_size) {
        return DISK_BAD_BUFFER;
    }
    if (command == DISK_WRITE && drive->read_only) {
        return DISK_READ_ONLY;
    }

    uint8_t *memory = vm->memory + disk->buffer;
    off_t offset = (off_t)disk->sector * DISK_SECTOR_SIZE;
    for (size_t done = 0; done < size;) {
        ssize_t moved = command == DISK_READ ? pread(drive->fd, memory + done, size - done, offset + (off_t)done)
                                             : pwrite(drive->fd, memory + done, size - done, offset + (off_t)done);
        if (moved > 0) {
            done += (size_t)moved;
        } else if (moved == 0 || errno != EINTR) {
            return DISK_HOST_ERROR;
        }
    }
    if (command == DISK_READ) {
        cpu_forget(vm, disk->buffer, (uint32_t)size);
    }
    return DISK_OK;
}

// Gives the image as many sectors as the count, adding zeros or cutting off the end
static uint32_t disk_resize(DiskState *disk, Drive *drive) {
    if (disk->count > DISK_MAX_SECTORS) {
        return DISK_BAD_SECTOR;
    }
    if (drive->read_only) {
        return DISK_READ_ONLY;
    }
    if (ftruncate(drive->fd, (off_t)disk->count * DISK_SECTOR_SIZE) != 0) {
        return DISK_HOST_ERROR;
    }
    drive->sectors = disk->count;
    return DISK_OK;
}

static uint32_t disk_command(VM *vm, DiskState *disk, uint32_t command) {
    if (command != DISK_READ && command != DISK_WRITE && command != DISK_RESIZE) {
        return DISK_BAD_COMMAND;
    }
    Drive *drive = selected(disk);
    if (!drive) {
        return DISK_NO_DISK;
    }
    return command == DISK_RESIZE ? disk_resize(disk, drive) : disk_transfer(vm, disk, drive, command);
}

// Ports 0 to 2 hold the first sector, the buffer address and the sector count of a transfer. Writing port 3
// runs a command and reading it gives the result, port 4 reads the disk size in sectors and port 5 selects
// the drive.
static uint32_t disk_read(VM *vm, IODevice *device, uint16_t offset) {
    DiskState *disk = device->state;
    Drive *drive = selected(disk);
    (void)vm;
    switch (offset) {
        case 0:
            return disk->sector;
        case 1:
            return disk->buffer;
        case 2:
            return disk->count;
        case 3:
            return disk->status;
        case 4:
            return drive ? drive->sectors : 0;
        default:
            return disk->drive;
    }
}

static void disk_write(VM *vm, IODevice *device, uint16_t offset, uint32_t value) {
    DiskState *disk = device->state;
    switch (offset) {
        case 0:
            disk->sector = value;
            break;
        case 1:
            disk->buffer = value;
            break;
        case 2:
            disk->count = value;
            break;
        case 3:
            disk->status = disk_command(vm, disk, value);
            break;
        case 5:
            disk->drive = value;
            break;
        default:
            break;
    }
}

static void disk_cleanup(VM *vm, IODevice *device) {
    DiskState *disk = device->state;
    (void)vm;
    for (int i = 0; i < DISK_DRIVES; i++) {
        if (disk->drives[i].fd >= 0) {
            close(disk->drives[i].fd);
            disk->drives[i].fd = -1;
        }
    }
}

// Images that cannot be written are attached read-only, a missing one is created empty, and a partial
// last sector is left out
int disk_attach(VM *vm, IODevice *device, int number, const char *path) {
    DiskState *disk = device->state;
    Drive *drive = &disk->drives[number];
    struct stat info;
    int read_only = 0, fd = open(path, O_RDWR);

    if (fd < 0 && errno == ENOENT) {
        fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0666);
    }
    if (fd < 0 && (errno == EACCES || errno == EROFS)) {
        read_only = 1;
        fd = open(path, O_RDONLY);
    }
    if (fd < 0 || fstat(fd, &info) != 0) {
        int error = errno;
        if (fd >= 0) {
            close(fd);
        }
        return vm_raise(vm, VM_ERROR_IO_ERROR, "%s: %s", path, strerror(error));
    }
    if (!S_ISREG(info.st_mode)) {
        close(fd);
        return vm_raise(vm, VM_ERROR_IO_ERROR, "%s: not a regular file", path);
    }

    if (drive->fd >= 0) {
        close(drive->fd);
    }
    uint64_t sectors = (uint64_t)info.st_size / DISK_SECTOR_SIZE;
    drive->fd = fd;
    drive->read_only = read_only;
    drive->sectors = sectors > UINT32_MAX ? UINT32_MAX : (uint32_t)sectors;
    return VM_ERROR_NONE;
}

int disk_device(VM *vm, IODevice *device) {
    DiskState *disk = calloc(1, sizeof(DiskState));
    *device = (IODevice){ .name = "disk", .base_port = IO_PORT_DISK, .port_count = 6,
                          .read = disk_read, .write = disk_write, .cleanup = disk_cleanup, .state = disk };
    if (!disk) {
        return vm_raise(vm, VM_ERROR_MEMORY_ALLOCATION, "Failed to allocate the disk");
    }
    for (int i = 0; i < DISK_DRIVES; i++) {
        disk->drives[i].fd = -1;
    }
    disk->count = 1;
    return VM_ERROR_NONE;
}
