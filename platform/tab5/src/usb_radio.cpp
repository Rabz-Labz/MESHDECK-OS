// usb_radio.cpp - see usb_radio.h.
#include "usb_radio.h"
#include <M5Unified.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

namespace {

UsbRadio* instance = nullptr;
portMUX_TYPE ringLock = portMUX_INITIALIZER_UNLOCKED;

void hostTask(void*) {
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

void clientTask(void* arg) { static_cast<UsbRadio*>(arg)->clientLoop(); }

void clientEvent(const usb_host_client_event_msg_t* msg, void* arg) {
    auto* self = static_cast<UsbRadio*>(arg);
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) self->onClientEvent(0, msg->new_dev.address);
    else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) self->onClientEvent(1, 0);
}

void inCallback(usb_transfer_t* t) { static_cast<UsbRadio*>(t->context)->onInDone(t); }
void outCallback(usb_transfer_t* t) { static_cast<UsbRadio*>(t->context)->onOutDone(t); }

uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

// ESP-IDF's own log lines (the USB host stack reports enumeration failures there) go to UART0,
// which isn't on the Tab5's USB port. Keep the last 4 KB of them for the RADIO LOG command.
constexpr int kLogRing = 4096;
char logRing[kLogRing];
int logHead = 0;
bool logWrapped = false;
portMUX_TYPE logLock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t previousLog = nullptr;

int captureLog(const char* fmt, va_list args) {
    char line[256];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    if (n > (int)sizeof line - 1) n = sizeof line - 1;
    portENTER_CRITICAL(&logLock);
    for (int i = 0; i < n; i++) {
        logRing[logHead] = line[i];
        logHead = (logHead + 1) % kLogRing;
        if (!logHead) logWrapped = true;
    }
    portEXIT_CRITICAL(&logLock);
    return previousLog ? previousLog(fmt, args) : n;
}

} // namespace

bool UsbRadio::begin() {
    instance = this;
    previousLog = esp_log_set_vprintf(captureLog);
    for (const char* tag : {"USBH", "HUB", "ENUM", "USB HOST", "HCD DWC", "EXT_HUB", "EXT_PORT"})
        esp_log_level_set(tag, ESP_LOG_DEBUG);
    M5.Power.setExtOutput(true, m5::ext_port_mask_t::ext_USB);   // USB-A 5 V (IO expander USB5V_EN)
    usb_host_config_t cfg = {};
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&cfg) != ESP_OK) return false;
    outDone_ = xSemaphoreCreateBinary();
    usb_host_client_config_t client = {};
    client.is_synchronous = false;
    client.max_num_event_msg = 5;
    client.async.client_event_callback = clientEvent;
    client.async.callback_arg = this;
    usb_host_client_handle_t handle;
    if (usb_host_client_register(&client, &handle) != ESP_OK) return false;
    client_ = handle;
    xTaskCreatePinnedToCore(hostTask, "usb_host", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(clientTask, "usb_radio", 4096, this, 5, nullptr, 0);
    return true;
}

void UsbRadio::onClientEvent(int event, uint8_t address) {
    if (event == 0) {
        newDevs_ = newDevs_ + 1;
        pendingAddress_ = address;
        openTries_ = 0;
    } else {
        gones_ = gones_ + 1;
        gone_ = true;
        ready_ = false;
    }
}

void UsbRadio::clientLoop() {
    for (;;) {
        usb_host_client_handle_events((usb_host_client_handle_t)client_, pdMS_TO_TICKS(50));
        if (gone_) {
            gone_ = false;
            closeDevice();
            goneAtMs_ = nowMs();
            portCycles_ = 0;
        }
        if (inHalted_ && device_) recoverIn();
        if (outHalted_ && device_) {
            outHalted_ = false;
            usb_device_handle_t dev = (usb_device_handle_t)device_;
            usb_host_endpoint_halt(dev, epOut_);
            usb_host_endpoint_flush(dev, epOut_);
            usb_host_endpoint_clear(dev, epOut_);
        }
        // A new device may not be ready to open at once: try up to 5 times, 200 ms apart.
        if (pendingAddress_ && !device_ && (int32_t)(nowMs() - retryAtMs_) >= 0) {
            uint8_t a = pendingAddress_;
            if (openDevice(a)) {
                pendingAddress_ = 0;
            } else if (++openTries_ >= 5) {
                pendingAddress_ = 0;
                goneAtMs_ = nowMs();   // the watchdog power-cycles the port to try again
            } else {
                retryAtMs_ = nowMs() + 200;
            }
        }
        watchdog();
    }
}

// The radio went (or never opened) and nothing came back: power the root port off and on, through
// the host library, so whatever is plugged in enumerates again. After 10, 20 and 30 s, then never
// until the next disconnect, so an empty port isn't cycled forever.
void UsbRadio::watchdog() {
    if (resetRequested_) {   // RADIO RESET: close whatever is open and cycle the port now
        resetRequested_ = false;
        closeDevice();
        goneAtMs_ = nowMs() - 10000;
        portCycles_ = 0;
    }
    if (device_ || !goneAtMs_ || portCycles_ >= 3) return;
    if (nowMs() - goneAtMs_ < 10000u * (portCycles_ + 1)) return;
    portCycles_++;
    totalCycles_ = totalCycles_ + 1;
    usb_host_lib_set_root_port_power(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    usb_host_lib_set_root_port_power(true);
}

// A failed IN transfer leaves its endpoint halted, and a transfer submitted to a halted endpoint
// never runs. Flush and clear the endpoint, then read again. Ten failures with no good transfer
// between them: close the device and let the watchdog power-cycle the port at once.
void UsbRadio::recoverIn() {
    inHalted_ = false;
    recoveries_ = recoveries_ + 1;
    usb_device_handle_t dev = (usb_device_handle_t)device_;
    if (++errStreak_ > 10) {
        closeDevice();
        goneAtMs_ = nowMs() - 10000;
        portCycles_ = 0;
        return;
    }
    usb_host_endpoint_halt(dev, epIn_);   // already halted after an error; harmless if so
    usb_host_endpoint_flush(dev, epIn_);
    usb_host_endpoint_clear(dev, epIn_);
    if (ready_ && inXfer_) inSubmit_ = usb_host_transfer_submit((usb_transfer_t*)inXfer_);
}

bool UsbRadio::openDevice(uint8_t address) {
    usb_device_handle_t dev;
    if (usb_host_device_open((usb_host_client_handle_t)client_, address, &dev) != ESP_OK) {
        openErr_ = 1;
        return false;
    }
    const usb_device_desc_t* dd;
    const usb_config_desc_t* cd;
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || usb_host_get_active_config_descriptor(dev, &cd) != ESP_OK) {
        usb_host_device_close((usb_host_client_handle_t)client_, dev);
        openErr_ = 2;
        return false;
    }
    vid_ = dd->idVendor;
    pid_ = dd->idProduct;

    // Walk the configuration: remember the CDC communication interface (class 0x02) and the
    // CDC data interface (class 0x0A) with its bulk IN and OUT endpoints.
    int offset = 0;
    const usb_standard_desc_t* d = (const usb_standard_desc_t*)cd;
    int currentClass = -1, currentIntf = -1;
    bool found = false;
    epIn_ = epOut_ = 0;
    while ((d = usb_parse_next_descriptor(d, cd->wTotalLength, &offset)) != nullptr) {
        if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t* in = (const usb_intf_desc_t*)d;
            currentClass = in->bInterfaceClass;
            currentIntf = in->bInterfaceNumber;
            if (currentClass == 0x02) commIntf_ = in->bInterfaceNumber;
        } else if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && currentClass == 0x0A) {
            const usb_ep_desc_t* ep = (const usb_ep_desc_t*)d;
            if ((ep->bmAttributes & 0x03) != 0x02) continue;   // bulk only
            if (ep->bEndpointAddress & 0x80) { epIn_ = ep->bEndpointAddress; mpsIn_ = ep->wMaxPacketSize; }
            else epOut_ = ep->bEndpointAddress;
            dataIntf_ = (uint8_t)currentIntf;
            found = epIn_ && epOut_;
        }
        if (found) break;
    }
    if (!found || usb_host_interface_claim((usb_host_client_handle_t)client_, dev, dataIntf_, 0) != ESP_OK) {
        usb_host_device_close((usb_host_client_handle_t)client_, dev);
        openErr_ = found ? 4 : 3;
        return false;
    }
    device_ = dev;

    // SET_CONTROL_LINE_STATE: DTR on, RTS off.
    usb_transfer_t* ctrl;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &ctrl) == ESP_OK) {
        usb_setup_packet_t* setup = (usb_setup_packet_t*)ctrl->data_buffer;
        setup->bmRequestType = 0x21;
        setup->bRequest = 0x22;
        setup->wValue = 0x0001;
        setup->wIndex = commIntf_;
        setup->wLength = 0;
        ctrl->num_bytes = sizeof(usb_setup_packet_t);
        ctrl->device_handle = dev;
        ctrl->bEndpointAddress = 0;
        ctrl->callback = outCallback;
        ctrl->context = this;
        ctrlResult_ = usb_host_transfer_submit_control((usb_host_client_handle_t)client_, ctrl);
        if (ctrlResult_ == ESP_OK) {
            for (int i = 0; i < 10 && xSemaphoreTake((SemaphoreHandle_t)outDone_, 0) != pdTRUE; i++)
                usb_host_client_handle_events((usb_host_client_handle_t)client_, pdMS_TO_TICKS(20));
            ctrlResult_ = 1000 + ctrl->status;   // 1000 = completed
        }
        usb_host_transfer_free(ctrl);
    }

    usb_transfer_t* in;
    usb_transfer_t* out;
    usb_host_transfer_alloc(512, 0, &in);
    usb_host_transfer_alloc(1024, 0, &out);
    in->device_handle = out->device_handle = dev;
    in->bEndpointAddress = epIn_;
    out->bEndpointAddress = epOut_;
    in->callback = inCallback;
    out->callback = outCallback;
    in->context = out->context = this;
    in->num_bytes = (512 / mpsIn_) * mpsIn_;
    inXfer_ = in;
    outXfer_ = out;
    // Start with an empty ring. Each volatile member is set with a plain assignment: C++20 deprecates
    // ++ and chained assignment on volatile, and the callbacks and the main loop share these values.
    head_ = 0;
    tail_ = 0;
    inHalted_ = false;
    errStreak_ = 0;
    goneAtMs_ = 0;
    portCycles_ = 0;
    openErr_ = 0;
    ready_ = true;
    inSubmit_ = usb_host_transfer_submit(in);
    return true;
}

void UsbRadio::closeDevice() {
    ready_ = false;
    if (!device_) return;
    usb_device_handle_t dev = (usb_device_handle_t)device_;
    // Cancel queued transfers first: an interface with transfers in flight can't be released, and a
    // device that isn't closed is never freed, so its port never takes a new one.
    const uint8_t eps[2] = {epIn_, epOut_};
    for (uint8_t ep : eps) {
        usb_host_endpoint_halt(dev, ep);
        usb_host_endpoint_flush(dev, ep);
    }
    // Let the cancelled (or NO_DEVICE) transfers come back before freeing them.
    for (int i = 0; i < 5; i++) usb_host_client_handle_events((usb_host_client_handle_t)client_, pdMS_TO_TICKS(20));
    // Keep the first failure, for the diagnostics line.
    esp_err_t err = usb_host_interface_release((usb_host_client_handle_t)client_, dev, dataIntf_);
    if (inXfer_ && usb_host_transfer_free((usb_transfer_t*)inXfer_) != ESP_OK && err == ESP_OK) err = ESP_FAIL;
    if (outXfer_ && usb_host_transfer_free((usb_transfer_t*)outXfer_) != ESP_OK && err == ESP_OK) err = ESP_FAIL;
    inXfer_ = outXfer_ = nullptr;
    esp_err_t closed = usb_host_device_close((usb_host_client_handle_t)client_, dev);
    closeErr_ = err != ESP_OK ? err : closed;
    device_ = nullptr;
    vid_ = pid_ = 0;
}

void UsbRadio::onInDone(void* transfer) {
    usb_transfer_t* t = (usb_transfer_t*)transfer;
    lastInStatus_ = t->status;
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
        if (t->status == USB_TRANSFER_STATUS_NO_DEVICE || t->status == USB_TRANSFER_STATUS_CANCELED) return;
        // Any other failure halts the endpoint: the client loop recovers it and reads again.
        inErrors_ = inErrors_ + 1;
        inHalted_ = true;
        return;
    }
    errStreak_ = 0;
    rxBytes_ += t->actual_num_bytes;
    // Keep the first bytes received, for the diagnostics line (up to the size of firstBytes_).
    for (int i = 0; i < t->actual_num_bytes && firstCount_ < (int)sizeof firstBytes_; i++) {
        firstBytes_[firstCount_] = t->data_buffer[i];
        firstCount_ = firstCount_ + 1;
    }
    portENTER_CRITICAL(&ringLock);
    for (int i = 0; i < t->actual_num_bytes; i++) {
        int next = (head_ + 1) % kRing;
        if (next == tail_) break;   // full: drop (the mesh parser resynchronises on frame headers)
        ring_[head_] = t->data_buffer[i];
        head_ = next;
    }
    portEXIT_CRITICAL(&ringLock);
    if (ready_) usb_host_transfer_submit(t);
}

void UsbRadio::onOutDone(void*) { xSemaphoreGive((SemaphoreHandle_t)outDone_); }

int UsbRadio::read(uint8_t* buf, int max) {
    int n = 0;
    portENTER_CRITICAL(&ringLock);
    while (n < max && tail_ != head_) { buf[n++] = ring_[tail_]; tail_ = (tail_ + 1) % kRing; }
    portEXIT_CRITICAL(&ringLock);
    return n;
}

bool UsbRadio::write(const uint8_t* data, int len) {
    if (!ready_ || !outXfer_ || len > 1024) return false;
    usb_transfer_t* t = (usb_transfer_t*)outXfer_;
    xSemaphoreTake((SemaphoreHandle_t)outDone_, 0);   // clear any stale completion
    memcpy(t->data_buffer, data, len);
    t->num_bytes = len;
    if (usb_host_transfer_submit(t) != ESP_OK) { txFail_ = txFail_ + 1; return false; }   // could not start: count it
    bool ok = xSemaphoreTake((SemaphoreHandle_t)outDone_, pdMS_TO_TICKS(500)) == pdTRUE &&
              t->status == USB_TRANSFER_STATUS_COMPLETED;
    // Count the outcome: a sent frame, or one that failed or timed out. A failed or stuck transfer
    // leaves the OUT endpoint halted or busy; the client loop clears it before the next write.
    if (ok) txOk_ = txOk_ + 1;
    else { txFail_ = txFail_ + 1; outHalted_ = true; }
    return ok;
}

std::string UsbRadio::diagnostics() const {
    char buf[400];
    int n = snprintf(buf, sizeof buf,
                     "data_if=%u comm_if=%u ep_in=%02X ep_out=%02X mps=%u ctrl=%d in_submit=%d rx=%u tx_ok=%u tx_fail=%u "
                     "in_err=%u in_status=%d new_dev=%u gone=%u recover=%u port_cycles=%u open_err=%d close_err=%d first=",
                     dataIntf_, commIntf_, epIn_, epOut_, mpsIn_, ctrlResult_, inSubmit_, (unsigned)rxBytes_,
                     (unsigned)txOk_, (unsigned)txFail_, (unsigned)inErrors_, lastInStatus_, (unsigned)newDevs_,
                     (unsigned)gones_, (unsigned)recoveries_, (unsigned)totalCycles_, openErr_, closeErr_);
    for (int i = 0; i < firstCount_ && n < (int)sizeof buf - 3; i++) n += snprintf(buf + n, sizeof buf - n, "%02X", firstBytes_[i]);
    return buf;
}

std::string UsbRadio::hostInfo() const {
    usb_host_lib_info_t info = {};
    if (usb_host_lib_info(&info) != ESP_OK) return "host=?";
    char buf[48];
    snprintf(buf, sizeof buf, "host_devices=%d host_clients=%d", info.num_devices, info.num_clients);
    return buf;
}

std::string UsbRadio::log() const {
    std::string out;
    portENTER_CRITICAL(&logLock);
    if (logWrapped) out.assign(logRing + logHead, kLogRing - logHead);
    out.append(logRing, logHead);
    portEXIT_CRITICAL(&logLock);
    return out;
}

std::string UsbRadio::describe() const {
    if (!ready_) return "NONE";
    char buf[32];
    snprintf(buf, sizeof buf, "%04X:%04X CDC", vid_, pid_);
    return buf;
}
