#pragma once

#include <string>
#include <vector>
#include "deps/hidapi/hidapi.h"

struct DeviceEntry {
    unsigned short vendor_id;
    unsigned short product_id;
    std::string path;
    std::string manufacturer;
    std::string product;
    std::string serial;
    std::string interface_name;
    unsigned short usage_page;
    unsigned short usage;
};

std::vector<DeviceEntry> enumerate_devices();
void run_monitor(const DeviceEntry &dev);
