#include "main.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <csignal>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string wcs_to_str(const wchar_t *ws)
{
    if (!ws) return "";
    std::string s;
    while (*ws) {
        wchar_t c = *ws++;
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else {
            s += '?'; // ASCII-only terminal output is sufficient here
        }
    }
    return s;
}

// ── sysfs helper ─────────────────────────────────────────────────────────────

// Reads the USB interface name from sysfs for a hidraw device.
// Path:  /sys/class/hidraw/<dev>/device  → HID node
//        dirname(HID node)/interface     → USB interface string
static std::string read_interface_name(const std::string &hidraw_dev)
{
    // hidraw_dev is e.g. "/dev/hidraw1" → basename "hidraw1"
    std::string devname = hidraw_dev.substr(hidraw_dev.rfind('/') + 1);
    std::string syspath = "/sys/class/hidraw/" + devname + "/device";

    char resolved[PATH_MAX];
    if (!realpath(syspath.c_str(), resolved))
        return "";

    // One level up = USB interface directory
    std::string usb_iface = resolved;
    auto slash = usb_iface.rfind('/');
    if (slash == std::string::npos)
        return "";
    usb_iface = usb_iface.substr(0, slash) + "/interface";

    FILE *f = fopen(usb_iface.c_str(), "r");
    if (!f)
        return "";

    char buf[256] = {};
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return ""; }
    fclose(f);

    std::string name(buf);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
        name.pop_back();
    return name;
}

// ── evdev grab ───────────────────────────────────────────────────────────────

// Recursively finds all eventX entries below 'dir' (no symlinks).
static void find_event_nodes(const std::string &dir, int depth,
                             std::vector<std::string> &out)
{
    if (depth > 6) return;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (strncmp(e->d_name, "event", 5) == 0 && isdigit(e->d_name[5])) {
            out.push_back("/dev/input/" + std::string(e->d_name));
        } else {
            std::string sub = dir + "/" + e->d_name;
            struct stat st;
            if (lstat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                find_event_nodes(sub, depth + 1, out);
        }
    }
    closedir(d);
}

// Exclusively grabs all evdev nodes of the USB device that hidraw_path belongs
// to via EVIOCGRAB. Returns all successfully grabbed fds.
// Strategy: HID node → USB interface → USB device → all eventX nodes below it.
static std::vector<int> grab_all_evdev(const std::string &hidraw_path)
{
    std::vector<int> fds;

    std::string devname = hidraw_path.substr(hidraw_path.rfind('/') + 1);
    std::string syspath = "/sys/class/hidraw/" + devname + "/device";

    char resolved[PATH_MAX];
    if (!realpath(syspath.c_str(), resolved))
        return fds;

    // HID node  →  USB interface (1 level up)  →  USB device (2 levels up)
    std::string p = resolved;
    for (int i = 0; i < 2; ++i) {
        auto slash = p.rfind('/');
        if (slash == std::string::npos) return fds;
        p = p.substr(0, slash);
    }
    // p now points to the USB device (e.g. .../usb3/3-2)

    std::vector<std::string> event_nodes;
    find_event_nodes(p, 0, event_nodes);

    for (const auto &path : event_nodes) {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGRAB, (void *)1) == 0)
            fds.push_back(fd);
        else
            close(fd);
    }
    return fds;
}

// ── device enumeration ───────────────────────────────────────────────────────

std::vector<DeviceEntry> enumerate_devices()
{
    std::vector<DeviceEntry> result;

    hid_device_info *devs = hid_enumerate(0x07C0, 0);
    for (hid_device_info *d = devs; d; d = d->next) {
        DeviceEntry e;
        e.vendor_id    = d->vendor_id;
        e.product_id   = d->product_id;
        e.path         = d->path ? d->path : "";
        e.manufacturer   = wcs_to_str(d->manufacturer_string);
        e.product        = wcs_to_str(d->product_string);
        e.serial         = wcs_to_str(d->serial_number);
        e.interface_name = read_interface_name(e.path);
        e.usage_page     = d->usage_page;
        e.usage          = d->usage;
        result.push_back(e);
    }
    hid_free_enumeration(devs);

    return result;
}

// ── signal handling ──────────────────────────────────────────────────────────

static volatile bool g_running = true;

static void on_sigint(int) { g_running = false; }

// ── monitor loop ─────────────────────────────────────────────────────────────

void run_monitor(const DeviceEntry &dev)
{
    hid_device *handle = hid_open_path(dev.path.c_str());
    if (!handle) {
        fprintf(stderr, "Error: could not open device: %s\n",
                dev.path.c_str());
        fprintf(stderr, "Hint: root privileges or a udev rule may be required.\n");
        return;
    }

    // Non-blocking reads so we can catch SIGINT cleanly
    hid_set_nonblocking(handle, 1);

    // Exclusively grab all evdev nodes of the USB device
    std::vector<int> evfds = grab_all_evdev(dev.path);

    signal(SIGINT, on_sigint);

    printf("\nReading from: %s (VID=%04X PID=%04X)\n",
           dev.path.c_str(), dev.vendor_id, dev.product_id);
    if (!evfds.empty())
        printf("%zu input interface(s) grabbed exclusively – keypresses will not reach the terminal.\n",
               evfds.size());
    else
        printf("Note: EVIOCGRAB failed – keypresses will still appear in the terminal.\n");
    printf("Press Ctrl+C to stop.\n\n");
    printf("%-6s  %s\n", "Bytes", "Raw data (hex)");
    printf("------  %s\n", std::string(48, '-').c_str());

    unsigned char buf[64];
    unsigned char prev[64] = {};
    int prev_n = 0;
    long packet_count = 0;

#define CLR_GREEN "\033[32m"
#define CLR_RESET "\033[0m"

    while (g_running) {
        int n = hid_read(handle, buf, sizeof(buf));

        if (n < 0) {
            fprintf(stderr, "\nRead error.\n");
            break;
        }
        if (n == 0) {
            // No packet available – sleep briefly
            struct timespec ts = {0, 1000000}; // 1 ms
            nanosleep(&ts, nullptr);
            continue;
        }

        ++packet_count;
        printf("[%05ld] %3d B  ", packet_count, n);

        // Hex section: highlight changed bytes in green
        for (int i = 0; i < n; ++i) {
            bool changed = (i >= prev_n || buf[i] != prev[i]);
            if (changed) printf(CLR_GREEN);
            printf("%02X ", buf[i]);
            if (changed) printf(CLR_RESET);
        }

        // Padding for ASCII section
        int pad = (16 - n) * 3;
        if (pad > 0) printf("%*s", pad, "");

        // ASCII section: highlight changed bytes in green
        printf(" |");
        for (int i = 0; i < n; ++i) {
            bool changed = (i >= prev_n || buf[i] != prev[i]);
            if (changed) printf(CLR_GREEN);
            putchar((buf[i] >= 0x20 && buf[i] < 0x7F) ? buf[i] : '.');
            if (changed) printf(CLR_RESET);
        }
        printf("|\n");
        fflush(stdout);

        memcpy(prev, buf, static_cast<size_t>(n));
        prev_n = n;
    }

#undef CLR_GREEN
#undef CLR_RESET

    printf("\n%ld packets received. Exiting.\n", packet_count);

    for (int fd : evfds) {
        ioctl(fd, EVIOCGRAB, (void *)0);
        close(fd);
    }
    hid_close(handle);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    if (hid_init() != 0) {
        fprintf(stderr, "hid_init() failed.\n");
        return 1;
    }

    // ── show device list ─────────────────────────────────────────────────────
    std::vector<DeviceEntry> devices = enumerate_devices();

    if (devices.empty()) {
        fprintf(stderr, "No KeyWarrior devices found (VID 07C0).\n");
        hid_exit();
        return 1;
    }

    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║              KeyWarrior Monitor – select device                      ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════╝\n\n");

    printf(" %-3s  %-6s  %-6s  %-4s  %-4s  %-10s  %-22s  %-24s  %s\n",
           "No", "VID", "PID", "UP", "U", "Device", "Interface", "Manufacturer", "Product");
    printf(" %s\n", std::string(100, '-').c_str());

    for (size_t i = 0; i < devices.size(); ++i) {
        const DeviceEntry &e = devices[i];

        std::string devnode = e.path;
        auto slash = devnode.rfind('/');
        if (slash != std::string::npos) devnode = devnode.substr(slash + 1);

        printf(" %-3zu  %04X    %04X    %04X  %04X  %-10s  %-22s  %-24s  %s\n",
               i + 1,
               e.vendor_id,
               e.product_id,
               e.usage_page,
               e.usage,
               devnode.c_str(),
               e.interface_name.empty() ? "-" : e.interface_name.c_str(),
               e.manufacturer.empty()   ? "(unknown)" : e.manufacturer.c_str(),
               e.product.empty()        ? "(unknown)" : e.product.c_str());
    }

    // ── selection ────────────────────────────────────────────────────────────
    printf("\nEnter number and press Enter: ");
    fflush(stdout);

    int choice = 0;
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > (int)devices.size()) {
        fprintf(stderr, "Invalid selection.\n");
        hid_exit();
        return 1;
    }

    const DeviceEntry &selected = devices[static_cast<size_t>(choice - 1)];

    printf("\nSelected: [%04X:%04X] %s – %s\n",
           selected.vendor_id, selected.product_id,
           selected.manufacturer.empty() ? "(unknown)" : selected.manufacturer.c_str(),
           selected.product.empty()      ? "(unknown)" : selected.product.c_str());

    // ── start monitor ────────────────────────────────────────────────────────
    run_monitor(selected);

    hid_exit();
    return 0;
}
