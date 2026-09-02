/*
 * Track-B Phase 0 PC probe (Windows / WinUSB).
 *
 * Opens the bulk-probe device (auto-bound to WinUSB via its MS OS 2.0 descriptor) and tight-polls
 * its bulk IN endpoint, measuring how many reads/sec the host can complete -- i.e. the effective
 * poll rate. That rate is the make-or-break Track-B number: if we sustain ~8-16k reads/sec, the
 * host can pull fresh input at ~60-125 us instead of the 1 kHz (500 us avg) interrupt-poll floor.
 *
 * Build (Developer Command Prompt):   cl probe.c /O2 /link winusb.lib setupapi.lib
 *   or MinGW:                          gcc probe.c -O2 -o probe.exe -lwinusb -lsetupapi
 * Run:                                 probe.exe [seconds]     (default 5)
 */
#include <windows.h>
#include <winusb.h>
#include <usbspec.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Must match the DeviceInterfaceGUID in the firmware's MS OS 2.0 descriptor.
static const GUID GUID_DEV = { 0x975F44D9, 0x0D08, 0x43FD, { 0x8B,0x3E,0x12,0x7C,0xA8,0xAF,0xFF,0x9D } };

static int find_path(char *out, size_t outsz) {
    HDEVINFO di = SetupDiGetClassDevs(&GUID_DEV, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return 0;
    SP_DEVICE_INTERFACE_DATA ifd; ifd.cbSize = sizeof(ifd);
    int found = 0;
    if (SetupDiEnumDeviceInterfaces(di, NULL, &GUID_DEV, 0, &ifd)) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(di, &ifd, NULL, 0, &need, NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A det = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(need);
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (SetupDiGetDeviceInterfaceDetailA(di, &ifd, det, need, NULL, NULL)) {
            strncpy(out, det->DevicePath, outsz - 1); out[outsz - 1] = 0; found = 1;
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(di);
    return found;
}

int main(int argc, char **argv) {
    double seconds = (argc > 1) ? atof(argv[1]) : 5.0;
    char path[512];
    if (!find_path(path, sizeof(path))) {
        printf("Device not found. Flash bulk_probe.uf2 and plug it into the PC.\n");
        return 1;
    }
    printf("device: %s\n", path);

    // WinUSB requires the handle be opened overlapped -- WinUsb_Initialize fails with
    // ERROR_INVALID_HANDLE (6) on a synchronous handle.
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("CreateFile failed: %lu\n", GetLastError()); return 1; }

    WINUSB_INTERFACE_HANDLE wu;
    if (!WinUsb_Initialize(h, &wu)) { printf("WinUsb_Initialize failed: %lu\n", GetLastError()); return 1; }

    USB_INTERFACE_DESCRIPTOR id;
    WinUsb_QueryInterfaceSettings(wu, 0, &id);
    UCHAR pipe = 0; ULONG maxpkt = 64;
    for (UCHAR i = 0; i < id.bNumEndpoints; i++) {
        WINUSB_PIPE_INFORMATION pi;
        if (WinUsb_QueryPipe(wu, 0, i, &pi) && USB_ENDPOINT_DIRECTION_IN(pi.PipeId) && pi.PipeType == UsbdPipeTypeBulk) {
            pipe = pi.PipeId; maxpkt = pi.MaximumPacketSize;
        }
    }
    if (!pipe) { printf("no bulk IN pipe\n"); return 1; }
    printf("bulk IN pipe 0x%02X, maxPacket %lu\n", pipe, maxpkt);

    UCHAR raw = 1;   // RAW_IO: no host-side buffering, lowest per-read overhead
    WinUsb_SetPipePolicy(wu, pipe, RAW_IO, sizeof(raw), &raw);

    uint8_t buf[512];
    uint64_t reads = 0, payloads = 0, drops = 0;
    uint32_t last = 0; int have = 0;
    OVERLAPPED ov; ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);   // overlapped read needs an event
    LARGE_INTEGER freq, t0, tn; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);

    printf("polling for %.1f s...\n", seconds);
    for (;;) {
        ULONG got = 0;
        if (!WinUsb_ReadPipe(wu, pipe, buf, maxpkt, &got, &ov)) {
            DWORD e = GetLastError();
            if (e == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(h, &ov, &got, TRUE)) { printf("GetOverlappedResult failed: %lu\n", GetLastError()); break; }
            } else { printf("ReadPipe failed: %lu\n", e); break; }
        }
        reads++;
        for (ULONG off = 0; off + 16 <= got; off += 16) {
            uint32_t seq; memcpy(&seq, buf + off, 4);
            if (have && seq > last + 1) drops += (seq - last - 1);
            last = seq; have = 1; payloads++;
        }
        QueryPerformanceCounter(&tn);
        double el = (double)(tn.QuadPart - t0.QuadPart) / freq.QuadPart;
        if (el >= seconds) {
            double rps = reads / el, pps = payloads / el;
            printf("\n=== %.2f s ===\n", el);
            printf("  reads (polls):     %llu  ->  %.0f reads/sec   (THE poll rate)\n", (unsigned long long)reads, rps);
            printf("  payloads:          %llu  ->  %.0f payloads/sec\n", (unsigned long long)payloads, pps);
            printf("  dropped payloads:  %llu\n", (unsigned long long)drops);
            printf("  implied latency:   ~%.0f us avg  (0.5 / poll-rate)\n", 0.5e6 / rps);
            printf("\n  vs USB-FS interrupt floor: 1000 reads/sec, ~500 us avg.\n");
            printf("  Track B is viable if reads/sec >> 1000 (target ~8000-16000).\n");
            break;
        }
    }
    WinUsb_Free(wu); CloseHandle(h);
    return 0;
}
