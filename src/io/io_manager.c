#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm_types.h"

// I/O Device Types
#define IO_DEVICE_CONSOLE     0
#define IO_DEVICE_DISK        1
#define IO_DEVICE_TIMER       2
#define IO_DEVICE_CUSTOM      100

// Maximum number of I/O devices
#define MAX_IO_DEVICES        16

// I/O device structure
typedef struct {
    uint8_t  type;            // Device type
    uint16_t base_port;       // Base I/O port
    uint16_t port_range;      // Number of ports used
    void    *device_data;     // Device-specific data
    
    // Device operations
    int     (*init)(VM *vm, void *device_data);
    void    (*cleanup)(VM *vm, void *device_data);
    uint32_t (*read)(VM *vm, void *device_data, uint16_t port);
    void     (*write)(VM *vm, void *device_data, uint16_t port, uint32_t value);
} IODevice;

// I/O devices container
typedef struct {
    IODevice devices[MAX_IO_DEVICES];
    int      device_count;
} IODevices;

// Console device operations
static int console_init(VM *vm, void *device_data) {
    // Nothing to initialize for console
    return VM_ERROR_NONE;
}

static void console_cleanup(VM *vm, void *device_data) {
    // Nothing to clean up for console
}

static uint32_t console_read(VM *vm, void *device_data, uint16_t port) {
    // Simple console input
    switch (port) {
        case 0:  // Standard input
            return getchar();
            
        case 1:  // Status (always ready for now)
            return 1;
            
        default:
            return 0;
    }
}

static void console_write(VM *vm, void *device_data, uint16_t port, uint32_t value) {
    // Simple console output
    switch (port) {
        case 0:  // Standard output
            putchar((int)(value & 0xFF));
            fflush(stdout);
            break;
            
        case 1:  // Standard error
            fprintf(stderr, "%c", (char)(value & 0xFF));
            fflush(stderr);
            break;
            
        default:
            // Ignore other ports
            break;
    }
}

// Timer device operations
static int timer_init(VM *vm, void *device_data) {
    // Initialize timer data
    if (!device_data) {
        device_data = calloc(1, sizeof(uint32_t));
        if (!device_data) {
            return VM_ERROR_MEMORY_ALLOCATION;
        }
    }
    
    // Reset timer value
    *((uint32_t *)device_data) = 0;
    
    return VM_ERROR_NONE;
}

static void timer_cleanup(VM *vm, void *device_data) {
    // Free timer data
    if (device_data) {
        free(device_data);
    }
}

static uint32_t timer_read(VM *vm, void *device_data, uint16_t port) {
    if (!device_data) {
        return 0;
    }
    
    uint32_t *timer_value = (uint32_t *)device_data;
    
    switch (port) {
        case 0:  // Timer value
            return *timer_value;
            
        default:
            return 0;
    }
}

static void timer_write(VM *vm, void *device_data, uint16_t port, uint32_t value) {
    if (!device_data) {
        return;
    }
    
    uint32_t *timer_value = (uint32_t *)device_data;
    
    switch (port) {
        case 0:  // Set timer value
            *timer_value = value;
            break;
            
        case 1:  // Control (1=start, 0=stop, 2=reset)
            if (value == 2) {
                *timer_value = 0;
            }
            // Start/stop would be handled by a separate thread in a real implementation
            break;
            
        default:
            // Ignore other ports
            break;
    }
}

// Initialize I/O system
int io_init(VM *vm) {
    if (!vm) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    // Allocate I/O devices container
    vm->io_devices = calloc(1, sizeof(IODevices));
    if (!vm->io_devices) {
        vm->last_error = VM_ERROR_MEMORY_ALLOCATION;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Failed to allocate memory for I/O devices");
        return VM_ERROR_MEMORY_ALLOCATION;
    }
    
    IODevices *io_devices = (IODevices *)vm->io_devices;
    io_devices->device_count = 0;
    
    // Add standard console device
    IODevice console_device = {
        .type = IO_DEVICE_CONSOLE,
        .base_port = 0,
        .port_range = 2,
        .device_data = NULL,
        .init = console_init,
        .cleanup = console_cleanup,
        .read = console_read,
        .write = console_write
    };
    
    io_add_device(vm, &console_device);
    
    // Add timer device
    IODevice timer_device = {
        .type = IO_DEVICE_TIMER,
        .base_port = 8,
        .port_range = 2,
        .device_data = NULL,
        .init = timer_init,
        .cleanup = timer_cleanup,
        .read = timer_read,
        .write = timer_write
    };
    
    io_add_device(vm, &timer_device);
    
    return VM_ERROR_NONE;
}

// Clean up I/O system
void io_cleanup(VM *vm) {
    if (!vm || !vm->io_devices) {
        return;
    }
    
    IODevices *io_devices = (IODevices *)vm->io_devices;
    
    // Clean up each device
    for (int i = 0; i < io_devices->device_count; i++) {
        IODevice *device = &io_devices->devices[i];
        
        // Call device cleanup function if available
        if (device->cleanup) {
            device->cleanup(vm, device->device_data);
        }
    }
    
    // Free I/O devices container
    free(vm->io_devices);
    vm->io_devices = NULL;
}

// Add a device to the I/O system
int io_add_device(VM *vm, IODevice *device) {
    if (!vm || !vm->io_devices || !device) {
        return VM_ERROR_INVALID_ADDRESS;
    }
    
    IODevices *io_devices = (IODevices *)vm->io_devices;
    
    // Check if we have room for another device
    if (io_devices->device_count >= MAX_IO_DEVICES) {
        vm->last_error = VM_ERROR_IO_ERROR;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "Maximum number of I/O devices reached");
        return VM_ERROR_IO_ERROR;
    }
    
    // Copy device configuration
    memcpy(&io_devices->devices[io_devices->device_count], device, sizeof(IODevice));
    
    // Initialize the device
    if (device->init) {
        int result = device->init(vm, device->device_data);
        if (result != VM_ERROR_NONE) {
            return result;
        }
    }
    
    // Increment device count
    io_devices->device_count++;
    
    return VM_ERROR_NONE;
}

// Find device that handles a specific port
static IODevice* io_find_device(VM *vm, uint16_t port) {
    if (!vm || !vm->io_devices) {
        return NULL;
    }
    
    IODevices *io_devices = (IODevices *)vm->io_devices;
    
    // Search for a device that handles this port
    for (int i = 0; i < io_devices->device_count; i++) {
        IODevice *device = &io_devices->devices[i];
        
        if (port >= device->base_port && port < device->base_port + device->port_range) {
            return device;
        }
    }
    
    return NULL;
}

// Read from an I/O port
uint32_t io_read(VM *vm, uint16_t port) {
    IODevice *device = io_find_device(vm, port);
    
    if (!device) {
        // No device handles this port
        vm->last_error = VM_ERROR_IO_ERROR;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "No device handles I/O port 0x%04X", port);
        return 0;
    }
    
    // Call device read function
    if (device->read) {
        uint16_t relative_port = port - device->base_port;
        return device->read(vm, device->device_data, relative_port);
    }
    
    return 0;
}

// Write to an I/O port
void io_write(VM *vm, uint16_t port, uint32_t value) {
    IODevice *device = io_find_device(vm, port);
    
    if (!device) {
        // No device handles this port
        vm->last_error = VM_ERROR_IO_ERROR;
        snprintf(vm->error_message, sizeof(vm->error_message), 
                 "No device handles I/O port 0x%04X", port);
        return;
    }
    
    // Call device write function
    if (device->write) {
        uint16_t relative_port = port - device->base_port;
        device->write(vm, device->device_data, relative_port, value);
    }
}

// Get I/O system status information
void io_get_status(VM *vm, char *buffer, size_t buffer_size) {
    if (!vm || !vm->io_devices || !buffer) {
        return;
    }
    
    IODevices *io_devices = (IODevices *)vm->io_devices;
    
    // Write status header
    snprintf(buffer, buffer_size, "I/O System Status:\n");
    
    // Adjust pointers for additional content
    size_t len = strlen(buffer);
    buffer += len;
    buffer_size -= len;
    
    // List devices
    snprintf(buffer, buffer_size, "Number of devices: %d\n", io_devices->device_count);
    
    len = strlen(buffer);
    buffer += len;
    buffer_size -= len;
    
    for (int i = 0; i < io_devices->device_count && buffer_size > 0; i++) {
        IODevice *device = &io_devices->devices[i];
        
        const char *type_str = "Unknown";
        switch (device->type) {
            case IO_DEVICE_CONSOLE: type_str = "Console"; break;
            case IO_DEVICE_DISK:    type_str = "Disk"; break;
            case IO_DEVICE_TIMER:   type_str = "Timer"; break;
            case IO_DEVICE_CUSTOM:  type_str = "Custom"; break;
        }
        
        snprintf(buffer, buffer_size, "%d: %s (ports 0x%04X-0x%04X)\n", 
                 i, type_str, device->base_port, device->base_port + device->port_range - 1);
        
        len = strlen(buffer);
        buffer += len;
        buffer_size -= len;
    }
}